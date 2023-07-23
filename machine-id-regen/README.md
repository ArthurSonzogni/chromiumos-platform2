# Machine ID Regen

Provides machine-id regeneration.

## Usage

`machine-id-regen --reason=REASON --minimum_age=AGE --machine_id_file=MACHINE_ID_PATH`

*    REASON: This argument is mandatory. It can be either 'network'
     or 'periodic'. This argument is mandatory.
*    AGE: Minimum time that should pass since last regeneration.
     If this condition is not met, regeneration will be skipped.
     (Default value: 0)
*    MACHINE_ID_PATH:Path to file in which regenerated machine-id
     should be stored.(Default value: /var/lib/dbus/machine-id)

## Output

Application will log its output to /var/log/messages via syslog.

Logs are preceded with 'machine-id-regen'.
