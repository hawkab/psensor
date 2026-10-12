# SPDX-License-Identifier: GPL-2.0-or-later
"""On-demand reader and incremental rolling totals for compact telemetry."""
from collections import deque
import heapq
import json
import os
from pathlib import Path
import sqlite3
import time

HZ = os.sysconf('SC_CLK_TCK')
NCPU = os.cpu_count() or 1
BOOT = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
STATE = Path(os.environ.get('XDG_STATE_HOME', str(Path.home() / '.local/state'))) / 'psensor-analysis'
WINDOW = 900


def cpu_percent(counts):
    total, idle, wait = counts
    return max(0, min(100, (total-idle-max(0,wait))/max(total,1)*100))


class RecentTotals:
    """Integer tick sums; monotonic queues maintain per-process peaks in O(1)."""
    def __init__(self):
        self.samples = deque(); self.totals = {}; self.covered = 0

    def add(self, sample):
        self.samples.append(sample); self.covered += sample['dt']
        for pid,start,ticks,rss in sample['activities']:
            key=(pid,start)
            state=self.totals.get(key)
            if state is None:
                state=[0,deque(),0,0];self.totals[key]=state
            state[0]+=ticks; state[2]=sample['ts']; state[3]=rss
            value=ticks/HZ/sample['dt']*100
            peaks=state[1]
            while peaks and peaks[-1][1]<=value: peaks.pop()
            peaks.append((sample['ts'],value))

    def expire(self, cutoff):
        while self.samples and self.samples[0]['ts']<=cutoff:
            sample=self.samples.popleft();self.covered-=sample['dt']
            for pid,start,ticks,rss in sample['activities']:
                key=(pid,start); state=self.totals[key];state[0]-=ticks
                while state[1] and state[1][0][0]<=cutoff: state[1].popleft()
                if state[0]<=0: del self.totals[key]

    def summary(self, cutoff):
        overlap={}; excluded=0
        if self.samples:
            first=self.samples[0]
            excluded=max(0,first['dt']-(first['ts']-cutoff))
            if excluded:
                overlap={(p[0],p[1]):p[2]*excluded/first['dt'] for p in first['activities']}
        covered=max(0,self.covered-excluded)
        result=[]
        for key,(ticks,peaks,last,rss) in self.totals.items():
            ticks-=overlap.get(key,0)
            if ticks>0: result.append((key,ticks/HZ,peaks[0][1] if peaks else 0,last,rss))
        return heapq.nlargest(15,result,key=lambda p:p[1]),covered


class SnapshotReader:
    def __init__(self, state=STATE):
        self.state=state;self.db=None;self.samples=deque();self.names={};self.recent=RecentTotals()
        self.last=0;self.cache=None;self.leased=-1e9;self.journal={'accessible':False};self.peak_cache=None;self.tables=None;self.read_at=-1e9

    def lease(self):
        now=time.monotonic()
        if now-self.leased<8: return
        self.state.mkdir(parents=True,exist_ok=True,mode=0o700)
        temporary=self.state/'viewer.json.tmp'
        temporary.write_text(json.dumps({'pid':os.getpid(),'boot':BOOT,'until':now+20},separators=(',',':')))
        temporary.chmod(0o600);temporary.replace(self.state/'viewer.json');self.leased=now

    def _connect(self):
        if self.db is None:
            self.db=sqlite3.connect((self.state/'history.sqlite3').as_uri()+'?mode=ro',uri=True,timeout=.05)
            self.db.execute('PRAGMA query_only=ON')

    def _name(self,key):
        if key not in self.names:
            row=self.db.execute('SELECT name FROM process_names WHERE boot=? AND pid=? AND start=?',(BOOT,*key)).fetchone()
            self.names[key]=row[0] if row else f'Процесс {key[0]}'
        return self.names[key]

    def _load(self):
        # Several visible windows share one reader and one refresh result.
        if self.cache and time.monotonic()-self.read_at<.25: return
        self.lease();self._connect()
        cutoff=time.time()-WINDOW-30
        tables=self.tables or {r[0] for r in self.db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        if 'samples_v2' in tables: self.tables=tables
        incoming=[]
        if 'samples_v2' in tables:
            for ts,dt,payload in self.db.execute('SELECT ts,dt,payload FROM samples_v2 WHERE boot=? AND ts>? ORDER BY ts',(BOOT,max(cutoff,self.last))):
                raw=json.loads(payload)
                incoming.append(dict(ts=ts,dt=dt,cpu=cpu_percent(raw['counts']),activities=raw['activities'],hardware=raw['hardware'],applications=raw['applications']))
        if 'samples' in tables and (not self.last or 'samples_v2' not in tables):
            first=incoming[0]['ts'] if incoming else float('inf')
            for payload, in self.db.execute('SELECT payload FROM samples WHERE boot=? AND ts>? AND ts<? ORDER BY ts',(BOOT,max(cutoff,self.last),first)):
                try:
                    raw=json.loads(payload)
                    rows=raw['processes'];dt=raw['dt']
                    for p in rows: self.names[(p['pid'],p['start'])]=p['name']
                    apps=raw.get('applications',[])
                    for p in apps: self.names[(p['pid'],p['start'])]=p['name']
                    incoming.append(dict(ts=raw['ts'],dt=dt,cpu=raw['cpu'],hardware=raw['hardware'],
                        activities=[[p['pid'],p['start'],round(p['cpu']/100*dt*HZ),round(p['rss_mb']*1024)] for p in rows],
                        applications=[[p['pid'],p['start']] for p in apps]))
                except (ValueError,KeyError): continue
            incoming.sort(key=lambda r:r['ts'])
        for sample in incoming:
            if sample['dt']<=0: continue
            for p in sample['activities']: self._name((p[0],p[1]))
            for p in sample['applications']: self._name(tuple(p))
            self.samples.append(sample);self.recent.add(sample);self.last=sample['ts']
        if incoming: self.cache=None
        self.recent.expire(self.last-WINDOW)
        while self.samples and self.samples[0]['ts']<self.last-WINDOW-30: self.samples.popleft()
        if len(self.names)>4096:
            needed={(p[0],p[1]) for s in self.samples for p in s['activities']}
            needed.update(tuple(p) for s in self.samples for p in s['applications'])
            self.names={k:v for k,v in self.names.items() if k in needed}
        self.read_at=time.monotonic()

    def _alive(self,key):
        try:
            with open(f'/proc/{key[0]}/stat') as f: raw=f.read()
            return int(raw[raw.rfind(')')+2:].split()[19])==key[1]
        except (OSError,ValueError,IndexError): return False

    def _current_rows(self,sample):
        return sorted([dict(pid=p[0],start=p[1],name=self.names[(p[0],p[1])],cpu=round(p[2]/HZ/sample['dt']*100,2),
             machine_cpu=round(p[2]/HZ/sample['dt']*100/NCPU,3),rss_mb=round(p[3]/1024,1)) for p in sample['activities'] if p[2]],key=lambda p:p['cpu'],reverse=True)

    def _contributors(self,hot):
        totals={}
        for s in self.samples:
            if s['ts']>hot['ts'] or s['ts']<=hot['ts']-30: continue
            portion=min(s['dt'],s['ts']-(hot['ts']-30))/s['dt']
            for pid,start,ticks,rss in s['activities']:
                state=totals.setdefault((pid,start),[0,0,rss])
                state[0]+=ticks*portion/HZ;state[1]=max(state[1],ticks/HZ/s['dt']*100)
        return [dict(pid=key[0],start=key[1],name=self.names[key],cpu_seconds=value[0],peak=value[1])
                for key,value in heapq.nlargest(5,totals.items(),key=lambda item:item[1][0])]

    def snapshot(self, details=False):
        try: self._load()
        except (sqlite3.Error,OSError,ValueError): return self.cache
        if not self.samples: return None
        if self.cache is None:
            now=self.samples[-1];active=[s for s in self.samples if s['ts']>now['ts']-WINDOW]
            totals,covered=self.recent.summary(now['ts']-WINDOW)
            recent=[dict(pid=key[0],start=key[1],name=self.names[key],cpu_seconds=seconds,peak=peak,
                last_seen=last,rss_mb=rss/1024,alive=self._alive(key),average=seconds/max(covered,.001)*100,
                machine_average=seconds/max(covered,.001)*100/NCPU) for key,seconds,peak,last,rss in totals]
            valid=[s for s in active if s['hardware']['temps'].get('Tctl') is not None]
            peak=None
            if valid:
                hot=max(valid,key=lambda s:s['hardware']['temps']['Tctl'])
                if self.peak_cache is None or self.peak_cache['ts']!=hot['ts']:
                    self.peak_cache={'ts':hot['ts'],'temperature':hot['hardware']['temps']['Tctl'],'contributors':self._contributors(hot)}
                peak=self.peak_cache
                for p in peak['contributors']: p['alive']=self._alive((p['pid'],p['start']))
            chiplet=[v for s in active for key,v in s['hardware']['temps'].items() if key in ('Tccd1','Tccd2')]
            current=dict(ts=now['ts'],dt=now['dt'],cpu=now['cpu'],hardware=now['hardware'],processes=self._current_rows(now),
                applications=[dict(pid=p[0],start=p[1],name=self.names[tuple(p)]) for p in now['applications']],journal=self.journal)
            self.cache=dict(version=2,logical_cpus=NCPU,boot_id=BOOT,current=current,recent=recent,covered_seconds=covered,peak=peak,
                temperature_range=(min(s['hardware']['temps']['Tctl'] for s in valid),max(s['hardware']['temps']['Tctl'] for s in valid)) if valid else None,
                chiplet_peak=max(chiplet) if chiplet else None)
        self.cache['current']['journal']=self.journal
        if details and 'series' not in self.cache:
            self.cache['series']=[[s['ts'],s['cpu'],*(s['hardware']['temps'].get(k) for k in ('Tctl','Tccd1','Tccd2'))] for s in self.samples if s['ts']>self.last-WINDOW]
            rows=self.cache['current']['processes'];lines=[]
            if rows: lines.append(f'Основной потребитель сейчас: {rows[0]["name"]} ({rows[0]["cpu"]:.1f}% одного потока)')
            if self.cache['current']['cpu']<12: lines.append('Малая общая загрузка не исключает нагрев отдельных ядер и недавние всплески')
            self.cache['analysis']=lines
        return self.cache

    def drop_details(self):
        if self.cache:
            self.cache.pop('series',None);self.cache.pop('analysis',None)

    def close(self):
        if self.db: self.db.close();self.db=None
        try:
            lease=self.state/'viewer.json'
            if json.loads(lease.read_text()).get('pid')==os.getpid(): lease.unlink()
        except (OSError,ValueError): pass
