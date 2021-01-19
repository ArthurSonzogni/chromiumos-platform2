# screen-capture-utils

Utilities for screen capturing for dev/test
images for working with screen capture.

## screenshot

Provides a screenshot of the current display. Useful for capturing what's on the
display when test has failed, for example.  Not all devices are supported yet,
so your mileage may vary.

## kmsvnc

VNC server using the same infrastructure as screenshot for grabbing display.

Here’s a quick rundown of how to use kmsvnc.

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
