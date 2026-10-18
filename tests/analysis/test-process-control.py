# SPDX-License-Identifier: GPL-2.0-or-later
"""Signal only disposable children; never contact polkit or production tasks."""
import os
from pathlib import Path
import signal
import subprocess
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'analysis'))
import process_control as control

assert 'gi' not in sys.modules
helper = ROOT/'src/psensor-process-terminate'
assert helper.is_file(), 'Build psensor-process-terminate before running these tests'


def start_time(pid):
    raw=Path(f'/proc/{pid}/stat').read_text()
    return int(raw[raw.rfind(')')+2:].split()[19])


def child_check(action):
    child = subprocess.Popen(['/bin/sleep','60'])
    try: action(child, start_time(child.pid))
    finally:
        if child.poll() is None: child.kill()
        child.wait(timeout=3)


def refuses_mismatch(child, start):
    try: control.terminate(child.pid,start+1)
    except control.ProcessEnded: pass
    else: raise AssertionError('reused PID was not rejected')
    assert child.poll() is None
    assert subprocess.run([helper,str(child.pid),str(start+1)]).returncode == 3
    assert child.poll() is None
    original_close = os.close
    with patch.object(control.signal,'pidfd_send_signal',side_effect=PermissionError('test denial')), \
         patch.object(control.os,'close',wraps=original_close) as closed:
        try: control.terminate(child.pid,start)
        except PermissionError: pass
        else: raise AssertionError('permission denial was hidden')
        assert closed.call_count == 1
    assert child.poll() is None


def native_terminates(child, start):
    assert subprocess.run([helper,str(child.pid),str(start)]).returncode == 0
    assert child.wait(timeout=3) == -signal.SIGTERM
    assert subprocess.run([helper,str(child.pid),str(start)]).returncode == 3


def python_terminates(child, start):
    control.terminate(child.pid,start)
    assert child.wait(timeout=3) == -signal.SIGTERM


child_check(refuses_mismatch)
child_check(native_terminates)
child_check(python_terminates)
for pid in (0,1,os.getpid()):
    try: control.terminate(pid,1)
    except ValueError: pass
    else: raise AssertionError('unsafe PID was accepted')
for args in ([],['1','1'],['0','1'],['-1','1'],['abc','1'],['99999999999999999999999','1']):
    assert subprocess.run([helper,*args]).returncode == 2
print('PASS: pidfd SIGTERM for disposable children, reused/ended PID rejection, permission errors, descriptor cleanup, safe argument validation; no GTK import')
