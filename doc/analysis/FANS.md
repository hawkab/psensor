# Optional IT8689 tachometer monitoring

Psensor already reads RPM from libsensors; the collector reads `fan*_input`
through hwmon. When firmware controls fan speed but no RPM sensor appears,
the missing part may be a Super I/O kernel driver.

On a Gigabyte **B650 AORUS ELITE AX / IT8689**, the stock driver did not expose
the fans. The unmodified [frankcrawford/it87](https://github.com/frankcrawford/it87)
driver at `bd7a566c8a60fe0308d67e26761e16260368191a` exposed two tachometers.
No `force_id`, ACPI conflict override, PWM write or firmware fan-curve change
was needed. Physical `CPU_FAN` versus case-fan mapping was not verified;
keep names such as `fan1` and `fan2` until the connector mapping is known.

`scripts/install-it87.sh` is an **explicit, separate, root-only** setup for this
verified board. It is not run by `make install` or the user installer. It fetches
the pinned driver and installs DKMS version `bd7a566.20261002` for the running
kernel, then configures module loading at boot. It requires Git, DKMS, matching
kernel headers and a module-signing arrangement when Secure Boot requires it.

```sh
sudo bash scripts/install-it87.sh
# Reboot, or explicitly load after checking existing hwmon drivers:
sudo modprobe it87
sensors
# Discover added sensors:
systemctl --user restart psensor-ui.service
```

The script refuses other boards. For another board/chip, inspect its driver
support before adapting the installation. The pinned module was tested on
kernel 7.0.0; compatibility with every later kernel is not implied.

Rollback the optional DKMS installation separately:

```sh
sudo dkms remove -m it87 -v bd7a566.20261002 --all
sudo rm /etc/modules-load.d/psensor-it87.conf
```

Reboot to use the original kernel module. Existing firmware fan control remains
in charge. Driver sources retain their upstream license and are not vendored
into this Psensor fork.
