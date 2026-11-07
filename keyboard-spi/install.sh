#!/bin/bash
# Install the macbook12-spi DKMS driver and remove the old script-based fix.
set -eu
[[ $EUID -eq 0 ]] || { echo "run with sudo"; exit 1; }
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG=macbook12-spi VER=0.1 DST=/usr/src/macbook12-spi-0.1

# Boot fix: load the SPI controller before the LPSS DMA engine so it probes in
# PIO mode (with DMA the applespi transfers time out).
install -m 644 "$here/applespi-order.conf" /etc/modprobe.d/applespi-order.conf

# Old userspace workaround — superseded by the driver.
rm -f /usr/lib/systemd/system-sleep/mb12-applespi-sleep /usr/local/sbin/mb12-lpss-regs /run/mb12-lpss-regs

dkms remove -m $PKG -v $VER --all 2>/dev/null || true
rm -rf "$DST"; mkdir -p "$DST"
cp "$here"/{dkms.conf,Makefile,spi-pxa2xx-pci.c,spi-pxa2xx.h,mb12-lpss-s3-context.patch,README.md,install.sh,uninstall.sh,applespi-order.conf} "$DST"/
dkms add -m $PKG -v $VER
dkms build -m $PKG -v $VER
dkms install -m $PKG -v $VER
update-initramfs -u
echo
modinfo -n spi_pxa2xx_pci
modinfo spi_pxa2xx_pci | grep '^version'
echo "Installed. Reboot to load the new driver."
