# ARC adbd

## ConfigFS / FunctionFS proxy for Developer Mode

This sets up the ADB gadget to allow Chromebooks that have the necessary
hardware / kernel support to be able to use ADB over USB. This avoids exposing
ConfigFS into the container.

See
https://android.googlesource.com/platform/system/core/+/HEAD/adb/daemon/usb.cpp
for more information.

## ADB over USB with DbC
xHCI (host mode) Debug capabilities can be used for adb functionalities without
switching to xDCI (device mode). This mode requires a USB 3.x A-to-C or C-to-C
cable to connect the host system to the debug target (ChromeOS device).
Alternatively, a USB 3.x A-to-A debug cable can be used.

To enable DbC, the kernel is built with CONFIG_USB_XHCI_DBGCAP config and the
DbC Vendor ID is updated to Google Vendor ID (0x18d1). For more details, refer
Documentation/ABI/testing/sysfs-bus-pci-drivers-xhci_hcd.

When DbC is configured, the /dev/ttyDBC0 serial device is created on the debug
target (Chromebook). In order to avoid exposing /dev to adbd, we create /dev/dbc
folder using tmpfiles and create an alias to ttyDBC0 node inside /dev/dbc using
udev rules.

The Dbc daemon file watches the dbc device node and sets up the ARCVM ADB bridge
for DbC when the serial device shows up. A Udev thread is also setup to monitor
usb hotplug events and handle the USB role changes required for host to host
mode setup.

## Configuration

This service expects a file in `/etc/arc/adbd.json` to configure the service.
The file should be a JSON with the following format:

```json
{
  # Required, the USB product identifier for the SoC.
  "usbProductId": "0x520B",
  # Optional, a list of kernel modules that need to be loaded prior to starting
  # to setup the USB gadget.
  "kernelModules": [
    # Each one of these objects will become an invocation to modprobe(8).
    {
      # Required, the name of the kernel module.
      "name": "g_ffs",
      # Optional, the list of additional parameters to modprobe(8). These can be
      # used to further configure the module.
      "parameters": [
        "functions=adb"
      ]
    }
  ],
  # Required, ADB over USB using DbC support enable
  "adbOverDbcSupport": true,
  # Required with DbC support enabled, PCI Bus Device ID for DbC
  "pciBusDeviceId": "0000:00:0d.0"
}
```
