# Event supportability

Healthd develops more and more
[events](https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/diagnostics/mojom/public/cros_healthd_events.mojom),
our clients use them as part of their diagnostic flow. However, our clients want
to know if a certain event is supported or not so that they can decide to render
an icon on the UI or not. Hence, we release an interface
`CrosHealthdEventService.IsEventSupported` for our clients to query the event
support status.

This document focuses on the following things:
- How we determine if an event is supported.
- What we need from OEMs to configure to make the event supported.

This document assumes that OEMs/ODMs understand how to make changes to the
Boxster config. If OEMs/ODMs have any trouble on this, please
[contact us][team-contact].

[team-contact]: mailto:cros-tdm-tpe-eng@google.com

[TOC]

## Command line interface

Some commands help you debug the issue or have a quick try:

1. `cros-health-tool event --help` Use this command to check all possible event
   types.
2. `cros-health-tool event --category=$EVENT --check_supported` Use this command
   to see if `$EVENT` is supported or not.

## Events

### USB

Always supported.

Spec (Partners only):
[Chromebook](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebook.html#usbc-gen-0001-v01),
[Convertible](https://chromeos.google.com/partner/dlm/docs/latest-requirements/convertible.html#usbc-gen-0001-v01),
[Detachable](https://chromeos.google.com/partner/dlm/docs/latest-requirements/detachable.html#usbc-gen-0012-v01),
[Chromeslate](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromeslate.html#usbc-gen-0005-v01),
[Chromebox](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebox.html#usbc-gen-0004-v01),
[Chromebase](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebase.html#usb-type-c-ports)

### Thunderbolt

Always supported. If any OEMs need to distinguish it with the USB, please
[reach out to us][team-contact].

### Lid

Supported only when `form-factor` is explicitly configured as one of the
following:
- CLAMSHELL
- CONVERTIBLE
- DETACHABLE

You can run the following commands on your DUT:
1. `cros_config /hardware-properties form-factor` This is helpful to understand
   what the value of `form-factor` is.
2. `cros-health-tool event --category=lid --check_supported` Use this to see if
   healthd reports the correct support status.

To configure `form-factor` in Boxster, you can use the `create_form_factor`
function defined in [hw_topology.star] to set it up.

### Bluetooth

Always supported.

Spec (Partners only):
[Chromebook](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebook.html#bt-gen-0001-v01),
[Convertible](https://chromeos.google.com/partner/dlm/docs/latest-requirements/convertible.html#bt-gen-0001-v01),
[Detachable](https://chromeos.google.com/partner/dlm/docs/latest-requirements/detachable.html#bt-gen-0001-v01),
[Chromeslate](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromeslate.html#bt-gen-0001-v01),
[Chromebox](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebox.html#bt-gen-0001-v01),
[Chromebase](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebase.html#bt-gen-0001-v01)

### Power

Always supported.

### Audio

Always supported. Chromebox may not have a speaker so the event may not be
suitable, if OEMs want us to report it as non-supported, please
[reach out to us][team-contact].

Spec (Partners only):
[Chromebook](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebook.html#spkr-gen-0007-v01),
[Convertible](https://chromeos.google.com/partner/dlm/docs/latest-requirements/convertible.html#spkr-gen-0003-v01),
[Detachable](https://chromeos.google.com/partner/dlm/docs/latest-requirements/detachable.html#spkr-gen-0001-v01),
[Chromeslate](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromeslate.html#spkr-gen-0005-v01),
[Chromebox](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebox.html#spkr-gen-0004-v01),
[Chromebase](https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebase.html#spkr-gen-0007-v01)

### Audio Jack

Supported only when `has-audio-jack` is explicitly configured as "true".

You can run the following commands on your DUT:
1. `cros_config /hardware-properties has-audio-jack` This is helpful to
   understand what the value of `has-audio-jack` is.
2. `cros-health-tool event --category=audio_jack --check_supported` Use this
   to see if healthd reports the correct support status.

To configure `has-audio-jack` in Boxster, you can use `create_audio` function
defined in [hw_topology.star] to set it up by filling up the `headphone_codec`
value to non `AUDIO_CODEC_UNKNOWN`. Because as long as there is a headphone
codec, the audio jack is supported. So we transform this into `has-audio-jack`
in chromeos-config.

### SD Card

Supported only when `has-sd-reader` is explicitly configured as "true".

You can run the following commands on your DUT:
1. `cros_config /hardware-properties has-sd-reader` This is helpful to
   understand what the value of `has-sd-reader` is.
2. `cros-health-tool event --category=sd_card --check_supported` Use this to see
   if healthd reports the correct support status.

To configure `has-sd-reader` in Boxster, you can use the `create_sd_reader`
function defined in [hw_topology.star] to set it up.

### Network

Not supported by the new event interface. Since there are no users at this
moment. Please [reach out to us][team-contact] if you need network events.

### Keyboard Diagnostic

Supported only when `form-factor` is explicitly configured as one of the
following:
- CLAMSHELL
- CONVERTIBLE
- DETACHABLE

You can run the following commands on your DUT:
1. `cros_config /hardware-properties form-factor` This is helpful to understand
   what the value of `form-factor` is.
2. `cros-health-tool event --category=keyboard_diagnostic --check_supported` Use
   this to see if healthd reports the correct support status.

To configure `form-factor` in Boxster, you can use the `create_form_factor`
function defined in [hw_topology.star] to set it up.

### Touchpad

Supported only when `form-factor` is explicitly configured as one of the
following:
- CLAMSHELL
- CONVERTIBLE
- DETACHABLE

You can run the following commands on your DUT:
1. `cros_config /hardware-properties form-factor` This is helpful to understand
   what the value of `form-factor` is.
2. `cros-health-tool event --category=touchpad --check_supported` Use this to
   see if healthd reports the correct support status.

To configure `form-factor` in Boxster, you can use `create_form_factor` function
defined in [hw_topology.star] to set it up.

### External Display

Currently the External Display event only supports detecting events based on
HDMI input. Therefore the event is supported only when `has-hdmi` is explicitly
configured as "true".

You can run the following commands on your DUT:
1. `cros_config /hardware-properties has-hdmi` This is helpful to understand
   what the value of `has-hdmi` is.
2. `cros-health-tool event --category=external_display --check_supported` Use this
   to see if healthd reports the correct support status.

To configure `has-hdmi` in Boxster, you can use the `create_hdmi` function
defined in [hw_topology.star] to set it up.

### Touchscreen

Supported only when `has-touchscreen` is explicitly configured as "true".

You can run the following commands on your DUT:
1. `cros_config /hardware-properties has-touchscreen` This is helpful to
   understand what the value of `has-touchscreen` is.
2. `cros-health-tool event --category=touchscreen --check_supported` Use this
   to see if healthd reports the correct support status.

To configure `has-touchscreen` in Boxster, you can use `create_screen` function
defined in [hw_topology.star] to set it up, in this case, you should fill the
`touch` parameter as `True`.

### Stylus Garage

Supported only when `stylus-category` is explicitly configured as "internal",
which means it's a "garaged stylus".

You can run the following commands on your DUT:
1. `cros_config /hardware-properties stylus-category` This is helpful to
   understand what the value of `stylus-category` is.
2. `cros-health-tool event --category=stylus_garage --check_supported` Use this
   to see if healthd reports the correct support status.

To configure `stylus-category` in Boxster, you can use the `create_stylus`
function defined in [hw_topology.star] to set it up.

### Stylus

Supported only when `stylus-category` is explicitly configured as "internal" or
"external".

You can run the following commands on your DUT:
1. `cros_config /hardware-properties stylus-category` This is helpful to
   understand what the value of `stylus-category` is.
2. `cros-health-tool event --category=stylus --check_supported` Use this to see
   if healthd reports the correct support status.

To configure `stylus-category` in Boxster, you can use the `create_stylus`
function defined in [hw_topology.star] to set it up.

[hw_topology.star]: https://chromium.googlesource.com/chromiumos/config/+/refs/heads/main/util/hw_topology.star
