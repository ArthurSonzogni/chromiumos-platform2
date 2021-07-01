# Ippusb Bridge

`ippusb_bridge` is a drop-in replacement for [`ippusbxd`][ippusbxd] for Chrome
OS. It provides support for connecting to IPP-over-USB printers, and forwarding
arbitrary protocols (IPP, eSCL, etc.) to those printers via HTTP.

The impetus for creating this was that `ippusbxd` has lost a lot of interest
since the release of [`ipp-usb`][ipp-usb], a similar project written in Golang.
Upstream provides only some oversight, and the code is pure C and has security
bugs.

Preferably, we would use `ipp-usb` on Chrome OS instead, but its binary size
(~9MB) precludes it.

*** aside
Googlers: see http://go/cros-image-size-process-proposal for context on why
9 MB is considered "too big" for inclusion.
***

Thus, `ippusb_bridge` is the happy medium: written in a safe language, to avoid
exposing unsafe code to all USB devices on the system, but still with a
reasonable binary size.

## Invocation

*   `ippusb_bridge` registers [udev rules that spawn it when `udevd` detects
    a USB printer plugged in][udev-rules].
*   The udev trigger [hands off invocation to upstart][upstart-bridge-start]
    for process lifecycle management.
*   Because it processes untrusted data (print jobs and other HTTP exchanges),
    [`ippusb_bridge` runs in a `minijail`ed environment with seccomp
    filters][seccomp-filters].

## General Operation

*   The primary goal of `ippusb_bridge` is to maintain a Unix socket in a
    predictable location, facilitating communication from either
    [`cupsd`][cupsd-ippusb-patch] or [`lorgnette`][lorgnette].
    *   This particular functionality is unique to the copy of CUPS packaged for
        Chromium OS.
    *   The socket's containing directory is `/run/ippusb` and its basename is
        built from its vendor ID and product ID: `${VID}-${PID}.sock`.
        These IDs propagate from the udev trigger [down to a command-line
        argument given to `ippusb_bridge`][unix-socket-argument].
*   `ippusb_bridge` continues running, ferrying messages back and forth across
    the given Unix socket, until the printer is unplugged.

## Miscellanea

*   `ippusb_bridge` depends on [the `tiny_http` crate][tiny_http]. `tiny_http`
    was patched
    *   to build without SSL features and
    *   to support operating over Unix sockets.

[ippusbxd]: https://www.github.com/OpenPrinting/ippusbxd
[ipp-usb]: https://github.com/OpenPrinting/ipp-usb
[tiny_http]: https://source.chromium.org/search?q=lang:ebuild+file:tiny_http&ss=chromiumos
[udev-rules]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/ippusb_bridge/udev/99-ippusb.rules
[upstart-bridge-start]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/ippusb_bridge/init/bridge_start
[seccomp-filters]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/ippusb_bridge/seccomp/
[cupsd-ippusb-patch]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/third_party/cups/backend/ipp.c;l=663;drc=277c6fad6c409edb86d4458338b991167c1e87d0
[lorgnette]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/lorgnette/
[unix-socket-argument]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/ippusb_bridge/src/arguments.rs;l=41;drc=3ac71c91bf3311868c4cc97dbd8f2983332667ac
