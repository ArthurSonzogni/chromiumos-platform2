# screen-capture-utils

Utilities for screen capturing for dev/test
images for working with screen capture.

[TOC]

## screenshot

Provides a screenshot of the current display. Useful for capturing what's on the
display when test has failed, for example.  Not all devices are supported yet,
so your mileage may vary.

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
(workstation)$ ssh -L 5900:localhost:5900 DUT
```

Then connect using a VNC client, such as [VNC Viewer for Google
Chrome](https://chrome.google.com/webstore/detail/vnc%C2%AE-viewer-for-google-ch/iabmpiboiopbgfabjmgeedhcmjenhbla)
to localhost:5900. It will ask you if you want to connect unauthenticated, here
we are relying on ssh forwarding to restrict who can connect.

## Development notes

### Building and testing

For development I typically deploy to /usr/local/ because tests expect them there.

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
