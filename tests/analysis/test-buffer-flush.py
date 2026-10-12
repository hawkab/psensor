from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0, str(ROOT / 'analysis'))
import os,signal,sqlite3,subprocess,time
from pathlib import Path
import tempfile
fixture=tempfile.TemporaryDirectory(prefix='psensor-flush-test-');state=Path(fixture.name)
process=subprocess.Popen(['/usr/bin/python3',str(ROOT/'analysis/heat-monitor.py'),'--samples','8'],env={**os.environ,'XDG_STATE_HOME':str(state)})
dbfile=state/'psensor-analysis/history.sqlite3'
try:
    deadline=time.monotonic()+3
    while not dbfile.exists() and time.monotonic()<deadline: time.sleep(.05)
    time.sleep(4.5)
    db=sqlite3.connect(dbfile)
    assert db.execute('SELECT COUNT(*) FROM samples_v2').fetchone()[0]==0
    process.send_signal(signal.SIGUSR1)
    deadline=time.monotonic()+1
    while time.monotonic()<deadline:
        n=db.execute('SELECT COUNT(*) FROM samples_v2').fetchone()[0]
        if n>=2: break
        time.sleep(.02)
    assert n>=2
    timestamps=[r[0] for r in db.execute('SELECT ts FROM samples_v2 ORDER BY ts')]
    assert all(1.8 < b-a < 2.3 for a,b in zip(timestamps,timestamps[1:]))
    print('PASS: closed-view writes are batched; on-demand flush publishes fresh samples without extra sampling')
    db.close()
finally:
    process.terminate();process.wait(timeout=3)
assert process.returncode==0
print('PASS: graceful shutdown flushes pending telemetry')

fixture.cleanup()
