# usb-debug-utils: extra scripts to help debug usb

Extra tools and scripts used when debugging USB.

- `usb_debug_utils.sh` will
  - mount `debugfs`
  - will install `usbmon` driver
  - will enable some dynamic debug messages
  - start a `tcpdump` process on each `usbmon` interface
