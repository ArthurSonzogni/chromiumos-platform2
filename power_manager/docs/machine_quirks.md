# Power Manager Machine Quirks

## Introduction

The role of Machine Quirks is to read DMI info from the kernel at runtime, and then activate the relevant prefs to that machine. This is necessary for the use case of the reven board, that supports a wide range of devices from different manufacturers. Further background can be found at [go/machine-quirks-design](go/machine-quirks-design).

## List of Machine Quirk Prefs

The following prefs are set in the overlay, and are used enable the various runtime checks that Machine Quirks performs.

| Machine Quirk Pref                        | Description |
|-------------------------------------------|-------------|
| suspend-to-idle-models                    | List of models for which suspend-to-idle should be enabled. |
| suspend-prevention-models                 | List of models for which disable-idle-suspend should be enabled. |
| external-display-only-models              | List of models for which external-display-only should be enabled. |
| exclude-from-external-display-only-models | List of models for which external-display-only should be disabled |
| has-machine-quirks                        | Pref that enables runtime pref selection capabilities of Machine Quirks. |

## Design & Implementation Details

On the machine, when powerd initializes, first it checks if the `kHasMachineQuirksPref` is activated by the board to see if it should move on.

Next, the [MachineQuirks class](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/power_manager/powerd/system/machine_quirks.cc) compares the values in prefs such as `kSuspendToIdleListPref` (and any other list prefs in the MachineQuirks class) to DMI info found in the following location:
```
/sys/class/dmi/id/
```
For most cases, it compares the value of `product_name` with the ListPref values. If a match is found, then the respective pref (example:`kSuspendToIdlePref`) is set to true via the Prefs class.


If multiple DMI values are listed for a model, then it checks each respective DMI file in that directory. It returns a match only if all DMI values match. Properly formatted examples of such quirk entries can be found in
[machine_quirks_test.cc](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/power_manager/powerd/system/machine_quirks_test.cc).

### Code:
[machine_quirks.h](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/power_manager/powerd/system/machine_quirks.h)\
[machine_quirks.cc](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/power_manager/powerd/system/machine_quirks.cc)\
[machine_quirks_stub.cc](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/power_manager/powerd/system/machine_quirks_stub.cc)\
[machine_quirks_stub.h](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/power_manager/powerd/system/machine_quirks_stub.h)\
[machine_quirks_test.cc](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/power_manager/powerd/system/machine_quirks_test.cc)

### Activation in Overlay

The MachineQuirks feature is currently activated via `chromeos-config` in the overlay, using `model.yaml` in `chromeos-config-bsp`.
In the future, activation may be moved to boxster.

## Testing

The machine quirk lists can be modified by creating files in `/var/lib/power_manager` to test out device fixes\*.

### Test suspend_to_idle_models
```
cat /sys/class/dmi/id/product_name > /var/lib/power_manager/suspend_to_idle_models
restart powerd
```
Refer to logs in `/var/log/power_manager/powerd.LATEST` and look at arguments passed into `powerd_setuid_helper` to confirm if the `--suspend-to-idle` flag was passed.

```
# after test, clear settings again
rm /var/lib/power_manager/suspend_to_idle_models
restart powerd
```
### Test suspend_prevention_models
```
cat /sys/class/dmi/id/product_name > /var/lib/power_manager/suspend_prevention_models
restart powerd
```
When suspend prevention is enabled, after suspend the power button remains on, and the screen immediately turns on upon user interaction on the keyboard.
```
# after test, clear settings again
rm /var/lib/power_manager/suspend_prevention_models
restart powerd
```

\*`has_machine_quirks` should be set to 1 for the above to work.

## Dynamic Machine Quirk(s)

There is also one dynamic machine quirk, kAllowZeroChargeReadOnACPref, which decides whether to activate the quirk on init rather than through a ListPref (kHasMachineQuirksPref still must be enabled). Instead of reading and comparing DMI info, this kAllowZeroChargeReadOnACPref checks if the device is using the generic ACPI battery driver, linux/drivers/acpi/battery.c.
