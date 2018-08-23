# Crash Collectors

For each major class of crash reports, we define a dedicated *collector*.
This is a simple way to encapsulate all related logic in a single module.

When we run [crash_reporter], depending on its mode, it simply iterates through
all registered collectors.

The [crash_collector.cc] code isn't a real collector, it's the base class to
hold common logic for all collectors.
Similarly, [user_collector_base.cc] isn't a real collector, it's the base class
to hold common logic for all user related collectors.

The [core_collector] program is just a utility tool and not a collector in the
sense of all these.
It probably should have used a different naming convention.

[TOC]

# Basic Operations

Each collector is designed to generate and queue crash reports.
They get uploaded periodically by [crash_sender].

# Boot Collectors

These are the collectors that run once at boot.
They are triggered via the [crash-boot-collect.conf] init service.
They do not, by design, block the boot of the system.
They are run in the background as a non-critical service.

## ec_collector

This collects [EC] (Chrome OS Embedded Controller) failures.

The program name is `embedded-controller` and might be referred to as `eccrash`.

*   The kernel driver [cros_ec_debugfs.c] sets up a debugfs path at
    `/sys/kernel/debug/cros_ec/`.
*   The driver probes the [EC] to see if it has any panic logs.
*   If the logs exist, the `/sys/kernel/debug/cros_ec/panicinfo` is created.
*   During boot, if that file exists, we read it and create a report.

[cros_ec_debugfs.c]: https://chromium.googlesource.com/chromiumos/third_party/kernel/+/chromeos-4.14/drivers/platform/chrome/cros_ec_debugfs.c

## kernel_collector

This collects kernel crashes that caused the system to reboot.

It is built on top of [pstore] and doesn't support any other data source.
We currently support the `ramoops` and `efi` backend drivers.

The program name is `kernel` and might be referred to as `kcrash`.

*   The BIOS/AP firmware maintain some dedicated space to hold a snippet of the
    kernel log.
    They make sure to not clear it during reboot in case there's valid data.
    *   For `ramoops`, CrOS firmware (e.g. coreboot) dedicate a chunk of RAM.
    *   For `efi`, the EFI firmware provides data in its own NVRAM space.
*   While the kernel is running normally, a circular buffer is used to hold the
    most recent portion of the kernel log buffer (i.e. what `printk` writes to
    and what `dmesg` reads from).
*   When the kernel reboots unexpectedly (e.g. due to a panic, oops, or BUG()),
    that error message is saved by [pstore] to the persistent location.
*   If the watchdog reset the system, we won't have an explicit panic message,
    but we will have the last snippet of the kernel log buffer.
*   During the next boot, the firmware makes sure that space is not reset.
*   While the kernel boots, the [pstore] driver will check that common space to
    see if there are any valid records.
    All valid records are made available via files in `/sys/fs/pstore/`.
*   During userspace boot, those paths are checked and reports are created.
*   For panics the kernel handled, we'll read the logs from `dmesg-ramoops-*`
    & `dmesg-efi-*`, and generate a report for each one.
*   Stack traces created by the kernel are analyzed to create a stack for the
    server, as well as generate a hash/fingerprint to correlate other reports.
*   For watchdog resets, we'll first query the eventlog (from [mosys]) to see if
    the reset was actually due to that.
    Normally we'd query the watchdog driver directly, but not all platforms are
    able to support that properly via the kernel driver.
    We'll create a simpler report using the last snippet of the kernel log from
    `console-ramoops-*` and hope the events just before the reset are enough to
    triage the problem.
*   As records are processed, they get removed from the pstore area.

## unclean_shutdown_collector

Collects unclean shutdown events.

*   On every boot, crash_reporter is run.
    It creates a file (`/var/lib/crash_reporter/pending_clean_shutdown`) to
    indicate that the system hasn't gone through a clean shutdown.
*   Upon clean shutdown ([chromeos_shutdown]), crash_reporter is run with the
    `--clean_shutdown` flag.
    The stateful partition file is removed to indicate the system has gone
    through a clean shutdown.
*   If during boot, the file already exists before crash_reporter attempts to
    create it, this signifies that the system hadn't shut down cleanly.
    A signal is enqueued for metrics_daemon to emit user metrics about this
    unclean shutdown.
*   No crash reports are otherwise generated for unclean shutdowns since it's
    not clear how we'd triage this in the first place (i.e. what to report).

# Runtime Collectors

Here are the collectors that are triggered on demand while the OS is running.
They are invoked either by the kernel or by other program.

## arc_collector

Collects [ARC++] crashes from programs inside the [ARC++] container.
This handles Android NDK and Java crashes.
It does not handle crashes from [ARC++] support daemons that run outside of the
container as those are collected like any other userland crash via the main
[user_collector].

[arc_collector] shares a lot of code with [user_collector] so it can overlay
[ARC++]-specific processing details.

## chrome_collector

Collects Chrome browser crashes.
The browser will hand us the minidump directly, so we only attach system
metadata and queue it.

crash_reporter will be called by the kernel for Chrome crashes like any other
[user_collector] crash, but we actually ignore these crashes.
Chrome is supposed to catch the crash in its parent process and handle it
itself; it links in [Google Breakpad] directly to do so.
This is because Chrome is better suited to know what memory regions to ignore
(e.g. large heaps or file memory maps or graphics buffers), as well as what
metadata to attach (e.g. the last URL visited, whether the process was a
renderer, browser, plugin, or other kind of process, `chrome://flags`, etc...).
Otherwise Chrome coredumps can easily consume 3GB+ of memory!

This does mean the system may miss crashes if Chrome's handling itself is buggy.

*** aside
In much older versions of Chrome OS (sometime before R40), Chrome would not only
handle creating its own crash reports, it would also handle uploading them.
We changed that behavior because Chrome's uploading is not as robust: it starts
uploading immediately, lacks delays/rate limiting, it tries only once, and if it
fails at all, it throws away the report entirely.
By queueing the report with crash-reporter, it avoids all those problems.
***

## udev_collector

This collects crash/error events triggered by [udev] events.
It is invoked via the [udev rules] and relies heavily on callbacks in the
[crash_reporter_logs.conf] file.

The program name is `udev`.

These reports are largely device specific as they try to capture whatever state
the device/firmware needs to triage.

TODO: Add devcoredump details if we ever enable them.

## user_collector

Collects all userland crashes where the kernel dumps core.
Basically any program that segfaults, aborts, violates a seccomp policy, or is
otherwise unceremoniously killed.

*   When a process crashes, the kernel invokes crash_reporter with various
    important runtime attributes (e.g. the pid, the uid, etc...).
    The kernel writes a full core dump of the process to stdin.
*   At this point, the failing process is frozen until crash_reporter exits.
    That means any parent that is monitoring the child won't be notified until
    we finish processing.
    This is often a critical path operation if a service needs to be restarted.
    *   Chrome reports are ignored normally; see the [chrome_collector] section
        for more details as to why.
*   The core2md is run to convert the full coredump to a minidump (`.dmp`).
    This process involves reading the core file contents to determine number of
    threads, register sets of all threads, and threads' stacks' contents.
    This is fundamental to our out-of-process design.
*   When a crash occurs, we consider the effective user ID of the process which
    crashed to determine where to save it.
    If the crashed process was running as `chronos`, we enqueue its crash to
    `/home/user/<user_hash>/crash/` which is on the user-specific cryptohome
    when a user is logged in since it might have user PII in it.
    If the crashed process was running as any other user, we enqueue the crash
    in `/var/spool/crash`.
*   The name of the crashing program is used to determine if we should gather
    additional diagnostic information.
    [crash_reporter_logs.conf] contains a list of executables and shell commands
    to run to gather more details.
    Any output from them will automatically be attached to the crash report as
    a `.log` file.

# Anomaly Collectors

Here are the collectors that [anomaly_collector] runs on syslog.
That service is spawned early during boot via [anomaly-collector.conf].

*   The [anomaly_collector] program runs a lexer on the syslog stream.
*   When certain regexes match, the lines get packaged up and saved to a file
    under `/run/anomaly-collector/` based on the specific collector.
*   crash_reporter is invoked for the specific collector.
    *   See sections below for more details.
*   [anomaly_collector] runs only one collector at a time, and waits for it to
    finish running fully before processing more syslog entries.

## kernel_warning_collector

Collects WARN() messages from anywhere in the depths of the kernel.
Could be drivers, subsystems, or core logic.

The program name is `kernel-warning` or `kernel-xxx-warning` (where `xxx` is a
common subsystem/area) and might be referred to as `kcrash`.

*   Whenever the kernel uses `WARN()` or `WARN_ON(...)` or any similar helper,
    it generates a standard log message including stack traces.
*   By default, `kernel-warning` is used everywhere, but the location of drivers
    in the backtrace are used to further refine the name.
*   The stack signature uses the same algorithm as the [kernel_collector].

## selinux_violation_collector

Collects [SELinux] policy violations.

The program name is `selinux-violation`.

*   Lines from the audit subsystem are processed.
*   Fields from each line are extracted (such as `name=` and `scontext=`) and
    used to create the magic signature.

## service_failure_collector

Collects warnings from the init (e.g. Upstart) for services that failed to
startup or exited unexpectedly at runtime.
This catches syntax errors in the init scripts and daemons that simply exit
non-zero but didn't otherwise trigger an abort or crash.

The program name is `service-failure`.

*   Lines from `init:` are processed.
*   The standard upstart syntax is:
    `<daemon> <job phase> process (<pid>) terminated with status <status>`.
*   All non-normal exits are recorded this way.
*   The signature is constructed from the exit status and service name.

[ARC++]: ../../arc/
[EC]: https://chromium.googlesource.com/chromiumos/platform/ec
[Google Breakpad]: https://chromium.googlesource.com/breakpad/breakpad
[mosys]: https://chromium.googlesource.com/chromiumos/platform/mosys/
[pstore]: https://chromium.googlesource.com/chromiumos/third_party/kernel/+/v4.17/Documentation/admin-guide/ramoops.rst
[SELinux]: https://en.wikipedia.org/wiki/Security-Enhanced_Linux
[udev]: https://en.wikipedia.org/wiki/Udev

[anomaly_collector]: ../anomaly_collector.l
[anomaly-collector.conf]: ../init/anomaly-collector.conf
[arc_collector]: ../arc_collector.cc
[chrome_collector]: ../chrome_collector.cc
[chromeos_shutdown]: ../../init/chromeos_shutdown
[core_collector]: ../core-collector/
[crash-boot-collect.conf]: ../init/crash-boot-collect.conf
[crash_collector.cc]: ../crash_collector.cc
[crash_reporter]: ../crash_reporter.cc
[crash_reporter_logs.conf]: ../crash_reporter_logs.conf
[crash_sender]: ../crash_sender.cc
[ec_collector]: ../ec_collector.cc
[kernel_collector]: ../kernel_collector.cc
[kernel_warning_collector]: ../kernel_warning_collector.cc
[selinux_violation_collector]: ../selinux_violation_collector.cc
[service_failure_collector]: ../service_failure_collector.cc
[udev rules]: ../99-crash-reporter.rules
[udev_collector]: ../udev_collector.cc
[unclean_shutdown_collector]: ../unclean_shutdown_collector.cc
[user_collector]: ../user_collector.cc
[user_collector_base.cc]: ../user_collector_base.cc
