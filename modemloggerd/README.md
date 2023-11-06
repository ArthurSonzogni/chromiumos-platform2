# Modem Logger Daemon

This daemon abstracts out the common portions of modem logging,
i.e. enabling logging functionality, triggering logging, and maintaining
log files. Currently, the daemon is meant for test images only.

## Modem-specific program API

In order to enforce a process boundary between `modemloggerd` and
third-party modem loggers, we farm out steps that
require modem-specific knowledge to different programs. `modemloggerd` will call
into these programs to request logging services.
