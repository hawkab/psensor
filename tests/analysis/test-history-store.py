from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0, str(ROOT / 'analysis'))
import importlib.util,json,math,random,sqlite3,sys,time
from pathlib import Path

from history_store import RecentTotals,SnapshotReader,BOOT,HZ
spec=importlib.util.spec_from_file_location('monitor',str(ROOT/'analysis/heat-monitor.py'));monitor=importlib.util.module_from_spec(spec);spec.loader.exec_module(monitor)
random.seed(9);roll=RecentTotals();all_samples=[];ts=1000
for i in range(700):
    dt=random.choice([1,2,3,7]);ts+=dt
    acts=[[600001,1,random.randrange(0,100),10],[600001,2,random.randrange(0,60),20]]
    acts=[p for p in acts if p[2]]
    s={'ts':ts,'dt':dt,'activities':acts};all_samples.append(s);roll.add(s);cutoff=ts-900;roll.expire(cutoff)
    if i%11: continue
    rows,covered=roll.summary(cutoff);actual={p[0]:(p[1],p[2]) for p in rows};expected={};duration=0
    for old in all_samples:
        kept=max(0,min(old['dt'],old['ts']-cutoff))
        if not kept: continue
        duration+=kept
        for pid,start,ticks,rss in old['activities']:
            total,peak=expected.get((pid,start),(0,0))
            expected[(pid,start)]=(total+ticks/HZ*kept/old['dt'],max(peak,ticks/HZ/old['dt']*100))
    assert abs(duration-covered)<1e-8
    assert actual.keys()==expected.keys()
    for key in expected: assert all(abs(a-b)<1e-8 for a,b in zip(actual[key],expected[key]))
print('PASS: incremental integer totals/peaks equal full-history reference; window boundaries, varying intervals and reused PIDs')
import tempfile
with_state = tempfile.TemporaryDirectory(prefix='psensor-history-test-')
state=Path(with_state.name)
db=sqlite3.connect(state/'history.sqlite3');db.execute('CREATE TABLE samples(ts REAL PRIMARY KEY,boot TEXT,payload TEXT)')
base=time.time()-20
hardware={'temps':{'Tctl':70,'Tccd1':76,'Tccd2':48},'fans':[],'gpus':[],'profile':'performance','power_profile':'performance'}
legacy={'ts':base,'dt':2,'cpu':1,'hardware':hardware,'applications':[],
    'processes':[dict(pid=600001,start=1,name='Gradle',cpu=100,rss_mb=1)]}
db.execute('INSERT INTO samples VALUES (?,?,?)',(base,BOOT,json.dumps(legacy)));db.commit();db.close()
writer=monitor.HistoryWriter(state);writer.name((600001,1),'Gradle',base+2)
writer.add(base+2,2,{'counts':[320,300,0],'activities':[[600001,1,200,1024]],'hardware':hardware,'applications':[[600001,1]]});writer.db.commit()
r=SnapshotReader(state);s=r.snapshot()
assert s and 'series' not in s and s['recent'][0]['cpu_seconds']==4
assert len(r.samples)==2 and s['current']['applications'][0]['name']=='Gradle'
writer.add(base+4,2,{'counts':[320,310,0],'activities':[[600001,1,100,1024]],'hardware':hardware,'applications':[]});writer.db.commit()
r.read_at=-1e9
s=r.snapshot();assert len(r.samples)==3 and abs(s['recent'][0]['cpu_seconds']-5)<1e-8
queries=[];r.db.set_trace_callback(queries.append)
again=r.snapshot();assert again is s and len(r.samples)==3 and not queries
full=r.snapshot(True);assert len(full['series'])==3
r.drop_details();assert 'series' not in s
r.close();writer.close();assert not (state/'viewer.json').exists()
print('PASS: legacy/compact history transition, incremental DB read without duplicates, names, lazy graph series, lease cleanup')

with_state.cleanup()
