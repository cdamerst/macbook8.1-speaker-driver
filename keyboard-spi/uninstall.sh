#!/bin/bash
# Remove the macbook12-spi DKMS driver (back to the stock spi-pxa2xx-pci).
set -eu
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
dkms remove -m macbook12-spi -v 0.1 --all || true
rm -rf /usr/src/macbook12-spi-0.1
# Pass --all to also remove the boot-order fix (keyboard may then be dead at boot).
[[ "${1:-}" == "--all" ]] && rm -f /etc/modprobe.d/applespi-order.conf
update-initramfs -u
echo "Removed. Reboot to load the stock driver."
