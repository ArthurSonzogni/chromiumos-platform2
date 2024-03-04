# Extended Auto Updates - ARC Cleanup Utility

## `/etc/init/extended-updates-arc-cleanup.conf`

Upstart job for cleaning up user's Android data on devices that have lost ARC
support after receiving Extended Auto Updates.

## `/usr/sbin/extended-updates-arc-remove-data`

Executable that performs the removal of user's Android data.
This binary is executed by the `extended-updates-arc-cleanup.conf` upstart job.
