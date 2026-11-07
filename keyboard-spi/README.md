# macbook12-spi — keyboard/touchpad at boot and after suspend (MacBook8,1)

## Problem
After S3 resume the MacBook 12" keyboard and touchpad (`applespi`, on the Intel
LPSS GSPI controller `0000:00:15.4`, `pxa2xx_spi_pci`) are dead: every transfer
fails with `SPI transfer timed out` (-110), so the lock screen gets no input.

## Cause
The LPSS block has a private register window at BAR0 + 0x800 holding the SPI
clock divider (`0x800`) and reset-release bits (`0x804`). Firmware programs it at
boot. When LPSS devices are enumerated through ACPI, `acpi_lpss.c` saves and
restores this context; in PCI mode (as on this Mac) nothing does. Measured on
this machine:

| reg   | before sleep | after S3   |
|-------|--------------|------------|
| 0x800 | 0x80190005   | 0x00000000 |
| 0x804 | 0x00000003   | 0x00000000 |
| 0x818 | 0x00000003   | 0x00000000 |

So the controller comes back held in reset with no clock.

## Fix
A patched `spi-pxa2xx-pci` (see `mb12-lpss-s3-context.patch`) wraps the core
driver's PM ops: it saves the same 9-register context `acpi_lpss` uses on
suspend/freeze/poweroff/runtime-suspend and restores it (reset release last)
on resume when it finds the context lost. On success it logs
`restored LPSS private context after power loss`.

Built and installed with DKMS, so it rebuilds on kernel updates. Source is
bundled (from Ubuntu linux-source 7.0.0); if a future kernel changes the
glue-driver API, the DKMS build fails and the stock module is used instead
(keyboard works, but not after suspend) — rebase the patch then.

## Boot fix (also installed)
`applespi-order.conf` → `/etc/modprobe.d/`:

    softdep dw_dmac_pci pre: spi_pxa2xx_pci

At boot the SPI controller must probe *before* the LPSS DMA engine
(`dw_dmac_pci`, `0000:00:15.0`) so it finds no DMA channels and runs in PIO mode
(`no DMA channels available, using PIO`). With DMA, every applespi transfer
times out and the keyboard/touchpad are dead from boot. Both fixes are needed.

## Install / remove
    sudo ./install.sh     # then reboot
    sudo ./uninstall.sh         # then reboot (keeps the boot fix)
    sudo ./uninstall.sh --all   # also removes the boot fix

`install.sh` also removes the earlier script workaround
(`/usr/lib/systemd/system-sleep/mb12-applespi-sleep`, `/usr/local/sbin/mb12-lpss-regs`).

## Check
    modinfo spi_pxa2xx_pci | grep version      # 0.1-mb12
    journalctl -k -b | grep -E 'restored LPSS|SPI transfer timed out'
