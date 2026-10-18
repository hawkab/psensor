#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run regression checks after configuring Psensor; GUI checks use private buses."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TESTS = Path(__file__).resolve().parent


def run(args, **kwargs):
    print('+', shlex.join(map(str,args)), flush=True)
    subprocess.run(list(map(str,args)), check=True, cwd=ROOT, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-gui', action='store_true', help='skip GTK and notification integration checks')
    args = parser.parse_args()
    env = {**os.environ, 'PYTHONPATH':str(ROOT/'analysis'), 'PYTHONDONTWRITEBYTECODE':'1',
           'GDK_BACKEND':'x11', 'GTK_USE_PORTAL':'0', 'GIO_USE_VFS':'local', 'NO_AT_BRIDGE':'1'}
    for name in ('test-model.py','test-history-store.py','test-buffer-flush.py','test-install-user.py','test-process-control.py'):
        run(['/usr/bin/python3', TESTS/name], env=env)
    packages = ['gtk+-3.0','ayatana-appindicator3-0.1']
    flags = shlex.split(subprocess.check_output(['pkg-config','--cflags',*packages],text=True))
    libs = shlex.split(subprocess.check_output(['pkg-config','--libs',*packages],text=True))
    with tempfile.TemporaryDirectory(prefix='psensor-native-tests-') as directory:
        outputs = Path(directory)
        common = [*shlex.split(os.environ.get('CC','cc')), '-O2','-g',
                  '-ffunction-sections','-fdata-sections', '-Wl,--gc-sections',
                  '-I'+str(ROOT), '-I'+str(ROOT/'src'), '-I'+str(ROOT/'src/lib'),
                  *shlex.split(os.environ.get('CPPFLAGS','')), *flags]
        for name, source in [('test-cooling','cooling_policy.c'),('test-actions','sensor_action.c'),('test-action-tab','action_tab.c'),
                             ('test-notification','action_notify.c'),('test-tray-allocation',None),
                             ('test-native-refresh',None),('test-cooling-runtime',None),('test-cooling-config',None)]:
            extra = [ROOT/'src/lib/temperature.c', ROOT/'src/cooling_tab.c'] if name == 'test-action-tab' else []
            run([*common, TESTS/(name+'.c'), *([ROOT/'src'/source] if source else []), *extra,
                 *libs, '-lm','-pthread','-o',outputs/name], env=env)
        for name in ('test-cooling','test-cooling-runtime','test-cooling-config','test-actions','test-tray-allocation','test-native-refresh'):
            run([outputs/name], env=env)
        if not args.no_gui:
            run(['dbus-run-session','--','xvfb-run','-a',outputs/'test-action-tab'], env=env)
            for name in ('test-lazy-ui.py','test-lifecycle.py','test-notification.py','test-process-menu.py'):
                case_env = {**env,'PSENSOR_TEST_NOTIFICATION_BINARY':str(outputs/'test-notification')}
                run(['dbus-run-session','--','xvfb-run','-a','/usr/bin/python3',TESTS/name], env=case_env)
    print('All analysis regression checks passed.')


if __name__ == '__main__':
    main()
