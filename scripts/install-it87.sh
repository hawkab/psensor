#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Optional DKMS setup for the board on which IT8689 tachometers were verified.
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'Run explicitly as root to install the kernel module.' >&2; exit 1; }
[[ $(cat /sys/class/dmi/id/board_name) == 'B650 AORUS ELITE AX' ]] || {
    echo 'This setup was verified only on B650 AORUS ELITE AX; see doc/analysis/FANS.md.' >&2; exit 1;
}
driver_commit=bd7a566c8a60fe0308d67e26761e16260368191a
driver_version=bd7a566.20261002
target_dir=/usr/src/it87-$driver_version
kernel_version=$(uname -r)
source_dir=$(mktemp -d)
trap 'rm -rf "$source_dir"' EXIT
git clone --quiet https://github.com/frankcrawford/it87.git "$source_dir"
git -C "$source_dir" checkout --quiet --detach "$driver_commit"
[[ $(git -C "$source_dir" rev-parse HEAD) == "$driver_commit" ]]
install -d -m 755 "$target_dir"
install -m 644 "$source_dir/it87.c" "$source_dir/compat.h" "$source_dir/Makefile" "$target_dir/"
printf '%s\n' "$driver_version" > "$target_dir/VERSION"
sed "s/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"$driver_version\"/; s@DEST_MODULE_LOCATION\[0\]=.*@DEST_MODULE_LOCATION[0]=\"/updates/dkms\"@" \
    "$source_dir/dkms.conf" > "$target_dir/dkms.conf"
if [[ ! -e /var/lib/dkms/it87/$driver_version/source ]]; then
    dkms add -m it87 -v "$driver_version"
fi
dkms build -m it87 -v "$driver_version" -k "$kernel_version"
dkms install -m it87 -v "$driver_version" -k "$kernel_version"
printf '%s\n' '# IT8689 tachometers; BIOS retains automatic fan control.' 'it87' > /etc/modules-load.d/psensor-it87.conf
echo 'Driver installed. No PWM values or BIOS fan curves were changed.'
echo 'Reboot or explicitly load the module, then restart Psensor to discover fans.'
dkms status -m it87
