# Chrome OS Keyboard Backlight Behavior

On devices that possess backlit keyboards, powerd is responsible for adjusting
the backlight brightness.

## Backlight triggers

For most devices, the backlight is turned on in response to the user activity
(including keyboard presses, touchpad events, or plugging/unplugging the device
into AC) instead. After user activity stops, the backlight remains on for
a period of time (the duration is supplied by the
`keyboard_backlight_keep_on_ms` preference, which defaults to 30 seconds), and
then fades to off.

A small number of devices have sensors capable of detecting when the user's
hands are hovering over it. For such devices, the backlight turns on when the
user's hands are hovering over the device, and then remains on for a further
period of time, again controlled by the `keyboard_backlight_keep_on_ms`
preference.

### Full screen video

When full screen video is detected, powerd turns the keyboard backlight off more
quickly so as to not distract the user. This time is configured by the
`keyboard_backlight_keep_on_during_video_ms` preference, which defaults to three
seconds.

## Backlight brightness

Powerd reads raw percentages from `keyboard_backlight_user_steps` preference,
scales the first step as 0%, second step as 10% and last step as 100%, and
calculates the rest of the scaled percentages linearly.

## Backlight Brightness and Ambient Light Sensor Behavior

As of M130, keyboard backlight brightness and keyboard ambient light sensor
settings are remembered and restored after a reboot.

### Out of Box Experience (OOBE)

During the device's first boot, if an ambient light sensor is present, powerd
uses its readings to determine the keyboard backlight brightness level. In a
well-lit environment, the backlight is turned off. In a dark environment, the
backlight is turned on at a moderate level (pursuant to user activity, as
described below). The ambient light ranges and corresponding backlight
brightness percentages are read from the `keyboard_backlight_als_steps`
preference. The percentages in this preference should be scaled percentages.

If no ambient light sensor is present, powerd reads a single brightness
percentage from the `keyboard_backlight_no_als_brightness` preference and uses
that instead when the backlight is turned on. The percentage in this preference
should be scaled percentage.

### During reboot

During reboot, the device restores previous keyboard backlight brightness
settings. These settings are saved per user profile and are restored when the
profile is loaded.

-   **Devices with ALS (Ambient Light Sensor):**

    -   *ALS last on*: Brightness adjusts automatically; the previous brightness
        percentage is not restored.

    -   *ALS last off*: The system restores the last user-set brightness
        percentage.

-   **Devices without ALS (Ambient Light Sensor):**

    -   Brightness always restores to the last user-set brightness percentage
        after a reboot.

### Brightness Control After Boot

#### Brightness control

Users can manually change the keyboard backlight brightness in three ways:

*   **Brightness keys:** The user is able to adjust the keyboard backlight
    brightness by holding Alt while pressing the Brightness Up or Brightness
    Down keys. The brightness moves between the raw percentage steps in the
    `keyboard_backlight_user_steps` preference. On the UI, the keyboard
    backlight brightness controller bar moves between the scaled percentage
    steps.

    On devices that have it on their keyboard, pressing the keyboard backlight
    toggle key turns the keyboard backlight on/off. Toggling the keyboard
    backlight on/off is functionally the same as forcing it on/off, with two
    differences. First, if a user-initiated brightness adjustment, e.g. an
    increase or decrease, is made while we're toggled off, we are no longer
    toggled off. Second, a brightness change signal is emitted any time the user
    changes the toggle state, even if the brightness percentage has not changed.

*   **Quick Settings:** Use the keyboard backlight brightness slider in the
    Quick Settings panel.

*   **Settings app:** Use the keyboard backlight brightness slider in the
    Keyboard settings.

Once the user has manually adjusted the brightness, powerd refrains from making
any more automated adjustments based on the ALS, and the ALS will be disabled
from making keyboard backlight brightness changes. The backlight will still be
dimmed or off for extended periods of inactivity, but this becomes based on the
longer timeouts used to dim the display, and not the shorter timeouts used by
default.

After reboot, if Chrome restores the keyboard backlight brightness to the last
user-set brightness percentage, this works the same as manually adjusting the
keyboard backlight brightness, and the backlight will be dimmed or off for
extended periods of inactivity based on the longer timeouts used to dim the
display.

#### Keyboard ambient light sensor control

User can toggle on/off Keyboard ALS in the Keyboard settings. Meanwhile,
manually adjusting the keyboard backlight brightness will disable Keyboard ALS.
If adjusted using the settings slider, the state is saved; otherwise, the change
is temporary and ALS will be re-enabled after a reboot. See
[Keyboard ambient light sensor re-enabled](#keyboard-ambient-light-sensor-re-enabled)
for more details.

#### Keyboard ambient light sensor re-enabled

Adjusting the keyboard backlight brightness by brightness keys or Quick settings
temporarily disables Keyboard ALS. ALS-based keyboard backlight brightness
control is automatically re-enabled after reboot to prevent accidental
deactivation.

The Keyboard ALS state is saved if and only if the user explicitly disables
Keyboard ALS in the Settings app or adjust brightness percent using Keyboard
settings slider.

## Historical behaviors

Prior to M130 (Q4 2024), keyboard backlight brightness behaved as follows:

*   Brightness percentage was not saved across reboot.

*   The ALS-based keyboard backlight brightness adjustment was disabled after
    any manual brightness adjustment and can't be re-enabled by user in the
    current boot session. Rebooting the device was the only way to re-enable it.

Prior to M52 (mid 2016), for devices without a hover sensor, the keyboard
backlight used to turn on and off in lock-step with the display.
https://crrev.com/c/340927 changed the behavior to turn off the backlight more
quickly after user activity ceased to reduce power consumption.
