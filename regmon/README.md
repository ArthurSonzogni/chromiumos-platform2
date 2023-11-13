# Policy Monitoring Daemon for ChromeOS

The Regmon daemon provides a d-bus interface for other components of ChromeOS to
report policy violations relating to first-party network traffic. The initial
reporting will come from Chrome, but as other components are instrumented they
will be documented here.

## Build and Deploy

```
~$ cros workon --board=${BOARD} start chromeos-base/regmon
(inside chroot)
~$ emerge-${BOARD} chromeos-base/regmon
~$ cros deploy ${DUT} regmon
```

## Design Docs

A more up-to-date design document will be added in this directory. The original
plans can be viewed at:
 - go/regulatory-netmon
