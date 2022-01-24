# screen-capture-utils

Utilities for screen capturing for dev/test images for working with screen
capture.

[TOC]

## screenshot

Provides a screenshot of the current display. Useful for capturing what's on the
display when test has failed, for example. Not all devices are supported yet, so
your mileage may vary.

## kmsvnc

VNC server using the same infrastructure as screenshot for grabbing display.

Here’s a quick rundown of how to use kmsvnc.

![kmsvnc usage diagram](kmsvnc-usage.png)

```shell
(DUT)# kmsvnc
```

VNC server will start listening on port 5900. Forward the port with SSH from
your client, and connect through the port. Example:

```shell
(workstation)$ ssh -L 5900:localhost:5900 DUT  # Keep this running to forward port.
```

Then connect using a VNC client. It could be any client but tigervnc-viewer
Debian package worked well from crostini on fizz. Make the client connect to
`localhost` port 5900 (which is display number 0, the parameter for
xtigervncviewer becomes `localhost:0`)

```shell
(workstation)$ sudo apt install tigervnc-viewer  # to install on Debian.
(workstation)$ xtigervncviewer localhost:0
```

### Reporting bugs

TODO(uekawa): set up component for crbug.

For Googlers please use http://go/kmsvnc-bug to file a bug. Current known issues
are available at http://b/hotlistid:2869886

## Development notes

### Building and testing

For development I typically deploy to /usr/local/ because tests expect them
there.

```
$ BOARD=rammus-arc-r
$ setup_board --board=${BOARD}  # required only once per board.
$ cros_workon --board=${BOARD} start screen-capture-utils
$ emerge-${BOARD} -j 100 chromeos-base/screen-capture-utils
$ cros deploy --root=/usr/local/ localhost:2229 chromeos-base/screen-capture-utils
$ tast run localhost:2229 graphics.KmsvncConnect
$ tast run localhost:2229 graphics.Smoke.platform
```

For debugging I typically need to deploy to /usr/sbin, from inside chroot

```
$ cros deploy localhost:2229 chromeos-base/screen-capture-utils
$ gdb-${BOARD} --remote=localhost:2229 /usr/sbin/kmsvnc
```

To run unit-tests

```
$ FEATURES=test emerge-$BOARD screen-capture-utils
```

### Running with more logs

With extra verbosity kmsvnc outputs things like fps logs. Use vmodule flag to
enable such extra logging.

```
kmsvnc --vmodule=kmsvnc=2
```
