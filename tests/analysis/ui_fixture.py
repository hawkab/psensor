# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic history for GTK checks: no dependency on a live collector/config."""
import importlib.util
import os
from pathlib import Path
import tempfile
import time


def setup():
    fixture = tempfile.TemporaryDirectory(prefix='psensor-ui-test-')
    os.environ['XDG_STATE_HOME'] = fixture.name
    os.environ['GDK_BACKEND'] = 'x11'
    os.environ['GTK_USE_PORTAL'] = '0'
    os.environ['GIO_USE_VFS'] = 'local'
    os.environ['NO_AT_BRIDGE'] = '1'
    source = Path(__file__).resolve().parents[2] / 'analysis/heat-monitor.py'
    spec = importlib.util.spec_from_file_location('fixture_monitor', source)
    monitor = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(monitor)
    state = Path(fixture.name) / 'psensor-analysis'
    state.mkdir()
    writer = monitor.HistoryWriter(state)
    pid = os.getpid()
    start = next(start for process, start in monitor.processes() if process == pid)
    ts = time.time() - 2
    writer.name((pid, start), 'Test process', ts)
    hardware = dict(temps={'Tctl':70,'Tccd1':76,'Tccd2':48}, fans=[], gpus=[],
                    profile='performance', power_profile='performance')
    writer.add(ts, 2, dict(counts=[320,300,0], activities=[[pid,start,100,1024]],
                          hardware=hardware, applications=[]))
    writer.close()
    return fixture
