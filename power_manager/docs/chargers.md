# Chargers

Some ChromeOS devices use Hybrid Power Boost chargers. These devices can only
draw power from an external power supply if the supply's voltage is greater
than or equal to a minimum required value.

This minimum voltage is a hardware property and is provided by the Embedded
Controller (EC) and read by `powerd` via the `min_charging_volt` pref. Only
devices with a Hybrid Power Boost charger provide this pref from their EC
firmware.

For other devices, `min_charging_volt` is not set in the EC firmware. `powerd`
uses a default value of 0.0 volts, effectively having no minimum charging
voltage requirement.
