# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='psensor-install-test-') as directory:
    root = Path(directory)
    prefix = root / 'runtime with spaces %'
    for name in ('bin/psensor', 'share/psensor-analysis/heat-monitor.py',
                 'share/psensor-analysis/load-window.py', 'share/psensor-analysis/icons/psensor_normal.png'):
        path = prefix / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('test fixture')
    env = {**os.environ, 'XDG_CONFIG_HOME':str(root/'config'), 'XDG_DATA_HOME':str(root/'data')}
    command = ['/usr/bin/python3', str(ROOT/'scripts/install-user.py'), '--runtime-prefix', str(prefix), '--no-register']
    subprocess.run(command, env=env, check=True, stdout=subprocess.DEVNULL)
    unit = root/'config/systemd/user/psensor-analysis.service'
    desktop = root/'data/applications/psensor.desktop'
    assert 'runtime with spaces %%' in unit.read_text()
    assert '%U' in desktop.read_text() and 'MimeType=x-scheme-handler/psensor;' in desktop.read_text()
    assert 'GDK_BACKEND=x11' in desktop.read_text()
    subprocess.run(['desktop-file-validate', str(desktop)], check=True)
    unit.write_text('previous user configuration')
    subprocess.run(command, env=env, check=True, stdout=subprocess.DEVNULL)
    backups = list(unit.parent.glob(unit.name+'.previous-*'))
    assert len(backups) == 1 and backups[0].read_text() == 'previous user configuration'
    subprocess.run(command, env=env, check=True, stdout=subprocess.DEVNULL)
    assert len(list(unit.parent.glob(unit.name+'.previous-*'))) == 1
print('PASS: per-user installation, spaced paths, URI registration, idempotence, previous configuration backup')
