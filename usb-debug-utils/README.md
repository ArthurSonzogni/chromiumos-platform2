# usb-debug-utils: extra scripts to help debug and test usb

Extra tools and scripts used when debugging and testing USB.

- `usb_debug_utils.sh` will
  - mount `debugfs`
  - will install `usbmon` driver
  - will enable some dynamic debug messages
  - start a `tcpdump` process on each `usbmon` interface

- `usb_compliance_utils.sh` will
  - install the `lvstest` driver and bind to a USB 3.0 root hub
  - enter and exit U3 link state
  - send GetDescriptor data packets
