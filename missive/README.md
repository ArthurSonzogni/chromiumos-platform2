# Missive Daemon

The Missive Daemon encrypts, stores, and forwards reporting records enqueued on
its DBus interface. The interface allows other CrOS daemons and Chrome to
enqueue records which are then encrypted and stored on disk. Records are
enqueued in different priority queues, which have different upload periods.
Once the storage daemon determines that a queue is ready for upload (i.e. the
requisite period has elapsed), it sends records to Chrome to be uploaded to
the Reporting Server. On successful upload, records are deleted from disk.

Note: This is a complementary daemon to the
`//chrome/browser/policy/messaging_layer` package in chromium.

## Build and Deploy

```
# HOST (inside chroot)
~$ cros_workon-${BOARD} start missive
~$ emerge-${BOARD} missive
~$ cros deploy root@${IP_ADDRESS} missive

# Until the missived ebuild is added to the overlay
# before starting missived copy the passwd/group
~$ scp /build/${BOARD}/etc/passwd  root@{IP_ADDRESS}:/etc/passwd
~$ scp /build/${BOARD}/etc/group  root@{IP_ADDRESS}:/etc/group

# DUT
~# start missived
```

## Original design docs

Note that aspects of the design may have evolved since the original design docs
were written.

* [Daemon Design](go/cros-reporting-daemon)
* [Messaging Layer](go/chrome-reporting-messaging-layer)
