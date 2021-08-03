# Chrome OS Power Management FAQ

[TOC]

## How do I prevent a system from going to sleep?

In M61 and later, there are settings at chrome://settings/power for controlling
the idle and lid-closed behaviors.

The [Keep Awake extension] can be used to quickly toggle between different idle
behaviors. It uses the [chrome.power API].

If the system is in dev mode, the `disable_idle_suspend` powerd pref can be used
to instruct powerd to not suspend the system in response to inactivity. This
pref is automatically set at `/usr/share/power_manager/disable_idle_suspend` in
dev or test images. To set it manually, write `1` to
`/var/lib/power_manager/disable_idle_suspend`. powerd will apply the updated
pref immediately.

Similarly, you can keep your development system awake while its lid is closed by
running the following as the `root` user:

```sh
# ectool forcelidopen 1
# echo 0 >/var/lib/power_manager/use_lid
# restart powerd
```

This should persist as long as you don't wipe the device's stateful partition.
To undo it, run the following:

```sh
# ectool forcelidopen 0
# rm -f /var/lib/power_manager/use_lid
# restart powerd
```

## How do I trigger a suspend manually?

The `powerd_dbus_suspend` program can be run from crosh or an SSH session to
exercise the normal suspend path; it sends a D-Bus message to powerd asking it
to suspend the system. See also `memory_suspend_test` and `suspend_stress_test`.

## How do I change power management timeouts for development or testing?

There are several different techniques that can be used to temporarily override
the power manager's default behavior:

### set_power_policy

This utility program was added in R26 to exercise the code path that Chrome uses
to override the default power management policy (which was needed for
enterprise). Run it with `--help` to see the available fields or without any
arguments to restore the default policy. Note that Chrome may override any
policy that you manually set; this happens when the related Chrome preferences
are changed or when Chrome is restarted, for instance.

### powerd preferences

powerd's default preferences are stored either in [chromeos-config] or on the
read-only partition in `/usr/share/power_manager` and
`/usr/share/power_manager/board_specific`. These preferences can be overridden
by identically-named files on the stateful partition in
`/var/lib/power_manager`. In most cases, preference changes won't take effect
until the powerd process is restarted.

To revert to the normal behavior, remove the files that you just created from
`/var/lib/power_manager` and restart the powerd job again.

For preferences configured in multiple sources, the order of preference is as
follows:

*   `/var/lib/power_manager`
*   [chromeos-config]
*   `/usr/share/power_manager/board_specific`
*   `/usr/share/power_manager`

To temporarily change prefs in an autotest, use [PowerPrefChanger].

### Changing powerd timeouts

`set_short_powerd_timeouts` script can be used to quickly set inactivity
timeouts to low values and restart powerd. To disable the power manager's
timeouts more permanently, run the following as root:

```sh
echo 1 >/var/lib/power_manager/ignore_external_policy
for i in {,un}plugged_{dim,off,suspend}_ms; do
  echo 0 >/var/lib/power_manager/$i
done
restart powerd
```

## How do I make my code run before the system suspends or after it resumes?

The power manager gives other daemons an opportunity to do any preparation that
they need to just before the system is suspended. See [suspend.proto] for a
detailed description of the process, along with the definitions of the protocol
buffers that are passed over D-Bus, and [suspend_delay_sample] for example
usage.

[Keep Awake extension]: https://chrome.google.com/webstore/detail/keep-awake-extension/bijihlabcfdnabacffofojgmehjdielb
[chrome.power API]: https://developer.chrome.com/extensions/power
[PowerPrefChanger]: https://chromium.googlesource.com/chromiumos/third_party/autotest/+/HEAD/client/cros/power/power_utils.py
[suspend.proto]: https://chromium.googlesource.com/chromiumos/platform2/system_api/+/HEAD/dbus/power_manager/suspend.proto
[suspend_delay_sample]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/power_manager/tools/suspend_delay_sample.cc
[chromeos-config]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/chromeos-config/

## How do I prevent the system from suspending or shutting down while my code is running?

This is often needed by code that updates firmware. Before powerd attempts to
suspend the system or shut it down, it checks for the presence of one or more
lockfiles, each containing the PID of a process that shouldn't be interrupted.
If it finds one, it defers the attempt for 10 seconds before trying again.

powerd uses several hard-coded lockfile paths within `/run/lock`, but new
lockfiles should be written to the `/run/lock/power_override` directory. Your
process should unlink its lockfile when it exits or no longer needs to block
power management.
