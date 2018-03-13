# Collecting VM logs

The [syslog](../syslog) directory implements a communication protocol for
gathering and persisting kernel and system logs for VMs.  This diagram shows the
flow of logs from the VM to the host system.

![VM logging](vm_logging.png)

## vmlog_forwarder

The first time `vm_launcher` receives a request to start a VM, it launches the
VM log forwarding service (`vmlog_forwarder`).  This service listens on a well
known port number on the host system for kernel and userspace logs from VMs.
Log entries are represented with the `LogRecord` protobuf message, which is
defined in [guest.proto](../proto/vm_guest.proto).

`vmlog_forwarder` converts `LogRecord` messages into an
[RFC3164](https://tools.ietf.org/html/rfc3164) syslog message and forwards it to
the host system's syslog daemon (`rsyslogd` on Chrome OS).  Additionally,
`vmlog_forwarder` scrubs the contents of each `LogRecord` to ensure that the
message contents contain only valid UTF-8 code points.  Control and
non-character code points are converted into a minimum 3-digit octal
representation while invalid codepoints are replaced with the UTF-8 replacement
character (U+fffd).

## vm_syslog

`vm_syslog` is a program that runs inside every VM and acts as the syslog daemon
for that VM, accepting [RFC3164](https://tools.ietf.org/html/rfc3164) compliant
messages from system processes running inside the VM as well as collecting VM
kernel log records from the `/dev/kmsg`
[interface](https://www.kernel.org/doc/Documentation/ABI/testing/dev-kmsg).

`vm_syslog` converts all log entries (both kernel and userspace) into
`LogRecord` messages before sending them out to the `vmlog_forwarder` service
running on the host system.  Readers may notice that `vm_syslog` accepts RFC3164
messages and `vmlog_forwarder` produces RFC3164 messages so it might seem
unnecessary to convert those entries into `LogRecord` messages.  However, every
VM is entirely untrusted and we'd prefer to rely on the well-tested protubuf
parsing code instead of trying to roll our own parser for untrusted messages.
