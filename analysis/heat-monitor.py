#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compact sampled history. Analysis and journal inspection run only in the UI."""
import argparse
import json
import os
from pathlib import Path
import signal
import sqlite3
import threading
import time

STATE = Path(os.environ.get('XDG_STATE_HOME', str(Path.home() / '.local/state'))) / 'psensor-analysis'
HZ = os.sysconf('SC_CLK_TCK')
PAGE_KIB = os.sysconf('SC_PAGE_SIZE') // 1024
BOOT = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
INTERVAL = 2.0

def read(path, default=''):
    try:
        with open(path) as f: return f.read().strip()
    except OSError: return default

def clean(value, size=60):
    return ''.join(c if c.isprintable() else ' ' for c in value).strip()[:size]


def identify(pid, comm):
    """Keep application labels, never store arbitrary command lines or secrets."""
    root = Path('/proc') / str(pid)
    try:
        with (root / 'cmdline').open('rb') as f:
            args = [a.decode(errors='replace') for a in f.read(8192).split(b'\0') if a]
    except OSError:
        args = []
    cwd = ''
    try:
        cwd = os.readlink(root / 'cwd')
    except OSError:
        pass
    if 'qemu-system' in comm:
        for flag in ('-avd', '-name'):
            if flag in args and args.index(flag) + 1 < len(args):
                return clean('Эмулятор · ' + args[args.index(flag) + 1])
        return clean('QEMU · ' + Path(cwd).name)
    if comm == 'java':
        if any('GradleDaemon' in a for a in args):
            return 'Gradle · ' + clean(Path(cwd).name, 32)
        if any('GradleWorkerMain' in a for a in args):
            return 'Gradle worker · ' + clean(Path(cwd).name, 32)
        if any('org.jetbrains' in a or 'com.intellij' in a for a in args):
            return 'IDE JetBrains'
        if any('atlassian' in a for a in args):
            return 'Jira · Java'
        jars = [Path(a).name for a in args if a.endswith('.jar') and not a.startswith('-')]
        return clean('Java · ' + jars[0]) if jars else 'Java'
    if comm in ('python', 'python3', 'MainThread'):
        scripts = [Path(a).name for a in args[1:] if a.endswith('.py')]
        if scripts:
            return clean(scripts[0])
    return clean(comm)


def processes():
    out = {}
    with os.scandir('/proc') as entries:
        for entry in entries:
            if not entry.name.isdigit(): continue
            try:
                # stat has a bounded fixed field set; one unbuffered read
                # avoids a second EOF syscall for every process.
                with open(entry.path + '/stat', 'rb', buffering=0) as f: text = f.read(4096)
                end = text.rfind(b')')
                fields = text[end + 2:].split()
                out[(int(entry.name), int(fields[19]))] = (int(fields[11]) + int(fields[12]),
                    text[text.find(b'(') + 1:end].decode(errors='replace'), int(fields[21]) * PAGE_KIB)
            except (OSError, ValueError, IndexError): continue
    return out


def machine_cpu():
    with open('/proc/stat', 'rb', buffering=0) as f:
        fields = tuple(map(int, f.read(512).split(b'\n',1)[0].split()[1:9]))
    return sum(fields), fields[3], fields[4]


class Hardware:
    def __init__(self):
        self.paths = []; self.discovered = -1e9; self.profile_at = -1e9
        self.profile = None; self.properties = None

    def discover(self):
        self.paths.clear()
        for root in Path('/sys/class/hwmon').glob('hwmon*'):
            name = read(root / 'name')
            if name == 'k10temp':
                for p in root.glob('temp*_input'):
                    label = read(p.with_name(p.name.replace('_input', '_label')), p.stem)
                    self.paths.append(('temp', str(p), label, name))
            for p in root.glob('fan*_input'):
                label = read(p.with_name(p.name.replace('_input', '_label')), p.stem.replace('_input', ''))
                self.paths.append(('fan', str(p), label, name))
            if name == 'amdgpu':
                self.paths.append(('gpu', str(root / 'device/gpu_busy_percent'), os.path.basename(os.path.realpath(root / 'device')), name))
        self.discovered = time.monotonic()

    def sample(self):
        now = time.monotonic()
        if now - self.discovered >= 60: self.discover()
        temps, fans, gpus = {}, [], []
        for kind, path, label, name in self.paths:
            raw = read(path)
            if not raw.lstrip('-').isdigit(): continue
            value = int(raw)
            if kind == 'temp': temps[label] = value / 1000
            elif kind == 'fan': fans.append({'chip': name, 'label': label, 'rpm': value, 'gpu': name == 'amdgpu'})
            else: gpus.append({'device': label, 'busy': value})
        epp = read('/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference', 'неизвестен')
        if now - self.profile_at >= 10:
            try:
                if self.properties is None:
                    import dbus
                    bus = dbus.SystemBus()
                    proxy = bus.get_object('org.freedesktop.UPower.PowerProfiles', '/org/freedesktop/UPower/PowerProfiles', introspect=False)
                    self.properties = dbus.Interface(proxy, 'org.freedesktop.DBus.Properties')
                self.profile = str(self.properties.Get('org.freedesktop.UPower.PowerProfiles', 'ActiveProfile', timeout=0.25))
            except Exception:
                self.properties = None
            self.profile_at = now
        return {'temps': temps, 'fans': fans, 'gpus': gpus, 'profile': epp, 'power_profile': self.profile or epp}


class HistoryWriter:
    def __init__(self, state):
        state.mkdir(parents=True, exist_ok=True, mode=0o700); state.chmod(0o700)
        self.state = state
        self.db = sqlite3.connect(state / 'history.sqlite3')
        (state / 'history.sqlite3').chmod(0o600)
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.execute('PRAGMA synchronous=NORMAL')
        self.db.execute('PRAGMA wal_autocheckpoint=256')
        self.db.execute('CREATE TABLE IF NOT EXISTS samples_v2 (ts REAL PRIMARY KEY, boot TEXT NOT NULL, dt REAL NOT NULL, payload TEXT NOT NULL)')
        self.db.execute('CREATE INDEX IF NOT EXISTS sample_boot_time ON samples_v2(boot,ts)')
        self.db.execute('CREATE TABLE IF NOT EXISTS process_names (boot TEXT, pid INTEGER, start INTEGER, name TEXT, last_seen REAL, PRIMARY KEY(boot,pid,start))')
        self.db.commit()
        self.committed = time.monotonic(); self.pruned = -1e9
        self.lease_mtime = None; self.lease_until = 0

    def name(self, key, name, ts):
        self.db.execute('INSERT OR REPLACE INTO process_names VALUES (?,?,?,?,?)', (BOOT, *key, name, ts))

    def add(self, ts, dt, payload):
        self.db.execute('INSERT INTO samples_v2 VALUES (?,?,?,?)', (ts, BOOT, dt, json.dumps(payload, ensure_ascii=False, separators=(',',':'))))
        now = time.monotonic()
        lease = self.state / 'viewer.json'
        try:
            modified = lease.stat().st_mtime_ns
            if modified != self.lease_mtime:
                self.lease_mtime = modified
                request = json.loads(lease.read_text())
                self.lease_until = request.get('until', 0) if request.get('boot') == BOOT else 0
        except (OSError, ValueError): self.lease_until = 0
        if now - self.pruned >= 300:
            cutoff = ts - 86400
            self.db.execute('DELETE FROM samples_v2 WHERE ts < ?', (cutoff,))
            self.db.execute('DELETE FROM process_names WHERE last_seen < ?', (cutoff,))
            if self.db.execute("SELECT 1 FROM sqlite_master WHERE name='samples'").fetchone():
                self.db.execute('DELETE FROM samples WHERE ts < ?', (cutoff,))
            self.pruned = now
        if now - self.committed >= 10 or now < self.lease_until:
            self.db.commit(); self.committed = now

    def close(self):
        self.db.commit(); self.db.close()


def run(count=0):
    writer = HistoryWriter(STATE); hardware = Hardware()
    oldcpu, oldprocs, oldmono = machine_cpu(), processes(), time.monotonic()
    olduptime = float(read('/proc/uptime').split()[0])
    labels = {}; iteration = 0; stop = False; requested = False
    wake = threading.Event()
    def terminate(signum, frame):
        nonlocal stop
        stop = True
        wake.set()
    def flush_requested(signum, frame):
        nonlocal requested
        requested = True
        wake.set()
    signal.signal(signal.SIGTERM, terminate); signal.signal(signal.SIGINT, terminate)
    signal.signal(signal.SIGUSR1, flush_requested)
    deadline = oldmono + INTERVAL
    try:
        while not stop and (not count or iteration < count):
            wake.wait(max(0, deadline - time.monotonic())); wake.clear()
            if stop: break
            if requested:
                writer.db.commit(); writer.committed = time.monotonic(); requested = False
            if time.monotonic() < deadline: continue
            mono = time.monotonic(); dt = mono - oldmono
            cpu, procs = machine_cpu(), processes(); ts = time.time()
            activities, applications = [], []
            for key, (ticks, comm, rss_kib) in procs.items():
                delta = max(0, ticks - oldprocs[key][0]) if key in oldprocs else (ticks if key[1] / HZ >= olduptime else 0)
                app = 'qemu-system' in comm or comm == 'java'
                if delta or app:
                    if key not in labels:
                        labels[key] = identify(key[0], comm)
                        writer.name(key, labels[key], ts)
                    if app and labels[key].startswith(('Gradle', 'Эмулятор', 'QEMU')): applications.append(list(key))
                if delta: activities.append([*key, delta, rss_kib])
            payload = {'counts': [cpu[i] - oldcpu[i] for i in range(3)], 'activities': activities,
                       'hardware': hardware.sample(), 'applications': applications}
            if iteration % 150 == 0:
                labels = {k:v for k,v in labels.items() if k in procs}
                writer.db.executemany('UPDATE process_names SET last_seen=? WHERE boot=? AND pid=? AND start=?', [(ts,BOOT,*k) for k in labels])
            writer.add(ts, dt, payload)
            oldcpu, oldprocs, oldmono = cpu, procs, mono
            olduptime = float(read('/proc/uptime').split()[0])
            deadline = mono + INTERVAL; iteration += 1
    finally: writer.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--samples', type=int, default=0)
    args = parser.parse_args(); run(args.samples)
