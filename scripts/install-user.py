#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Register a built/installed Psensor for the current desktop user."""
import argparse
from datetime import datetime
import os
from pathlib import Path
import shutil
import subprocess


def systemd_token(value):
    return '"' + str(value).replace('%', '%%').replace('$', '$$').replace('\\', '\\\\').replace('"', '\\"') + '"'


def desktop_token(value):
    # Exec quoting is decoded after Desktop Entry string escaping.
    value = str(value).replace('%', '%%')
    for char in ('\\', '"', '`', '$'):
        value = value.replace(char, '\\' + char)
    return '"' + value.replace('\\', '\\\\') + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    default = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'psensor-analysis/runtime'
    parser.add_argument('--runtime-prefix', type=Path, default=default)
    parser.add_argument('--start', action='store_true', help='start/restart both user services after registration')
    parser.add_argument('--no-register', action='store_true', help='write files only, without contacting the desktop or systemd')
    args = parser.parse_args()
    if args.start and args.no_register:
        parser.error('--start requires desktop registration')
    prefix = args.runtime_prefix.expanduser().resolve()
    binary = prefix / 'bin/psensor'
    analysis = prefix / 'share/psensor-analysis'
    for file in (binary, analysis / 'heat-monitor.py', analysis / 'load-window.py', analysis / 'icons/psensor_normal.png'):
        if not file.is_file():
            parser.error(f'Missing {file}; run configure, make and make install first')
    config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
    data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    units = config / 'systemd/user'
    applications = data / 'applications'
    files = {
        units / 'psensor-ui.service': f'''[Unit]
Description=Psensor with responsive GTK updates and heat analysis
After=graphical-session.target psensor-analysis.service
PartOf=graphical-session.target

[Service]
Type=simple
Environment=GDK_BACKEND=x11
ExecStart={systemd_token(binary)}
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
''',
        units / 'psensor-analysis.service': f'''[Unit]
Description=Compact CPU and temperature history for Psensor

[Service]
Type=simple
ExecStart=/usr/bin/python3 {systemd_token(analysis / 'heat-monitor.py')}
Restart=on-failure
RestartSec=5
Nice=10
NoNewPrivileges=true
UMask=0077

[Install]
WantedBy=default.target
''',
        applications / 'psensor.desktop': f'''[Desktop Entry]
Type=Application
Version=1.0
Name=Psensor
Comment=Temperature monitoring and heat analysis
Keywords=temperature;fan;monitoring;indicator;
Icon={analysis / 'icons/psensor_normal.png'}
Exec=/usr/bin/env GDK_BACKEND=x11 {desktop_token(binary)} %U
MimeType=x-scheme-handler/psensor;
Categories=System;Monitor;
StartupNotify=true
''',
        config / 'autostart/psensor.desktop': '''[Desktop Entry]
Type=Application
Name=Psensor
Exec=/usr/bin/systemctl --user start psensor-ui.service
X-GNOME-Autostart-enabled=true
X-GNOME-Autostart-Delay=30
''',
    }
    suffix = '.previous-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    for path, content in files.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists() and path.read_text() != content:
            shutil.copy2(path, path.with_name(path.name + suffix))
        path.write_text(content)
        path.chmod(0o644)
        print(path)
    if not args.no_register:
        subprocess.run(['update-desktop-database', str(applications)], check=True)
        subprocess.run(['xdg-mime', 'default', 'psensor.desktop', 'x-scheme-handler/psensor'], check=True)
        subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True)
        subprocess.run(['systemctl', '--user', 'enable', 'psensor-analysis.service', 'psensor-ui.service'], check=True)
        if args.start:
            subprocess.run(['systemctl', '--user', 'restart', 'psensor-analysis.service', 'psensor-ui.service'], check=True)


if __name__ == '__main__':
    main()
