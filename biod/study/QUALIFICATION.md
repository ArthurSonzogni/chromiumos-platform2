<!-- Format this doc with `mdformat --compatibility --w QUALIFICATION.md`. -->

# Fingerprint System Performance Analysis for Qualification

This document gives a high level overview of what is required to evaluate the
performance of a fingerprint system for Chrome OS qualification. In this
document, the *fingerprint system* is the combination of the fingerprint sensor,
fingerprint MCU (FPMCU), and matching library that runs on the FPMCU.

## Requirements

1.  An **FPMCU firmware** that is approved by the Chrome OS Fingerprint Team

    This should include the matching library and any relevant changes necessary
    to enable full fingerprint functionality within the Chrome OS User
    Interface.

    *Although the fingerprint study tool will collect "raw" fingerprint captures
    from the sensor [bypassing the matching library], it is important for the
    study participants to familiarize themselves with the fingerprint unlock
    feature on Chrome OS, before collecting samples for analysis. To achieve
    this, the participants will enroll their finger(s) on the Chromebook and use
    it to unlock the device multiple times.*

    The final firmware for a qualification must be built by the Chrome OS
    Fingerprint Team. If the matching library or any code that impacts the
    performance of the fingerprint system changes after qualification, a new
    qualification would be required.

2.  A **Chromebook** that is fitted with the fingerprint sensor and FPMCU

    The fingerprint sensor must be positioned in a natural location that is
    approved by Chrome OS.

    For qualification, the testing lab will require at least three identical
    Chromebook test devices to increase testing speed and redundancy.

3.  A **Chrome OS image** with the Fingerprint Study Tool enabled

    This is a Chrome OS image file that will be used to install Chrome OS on the
    Chromebook being tested. This image must include the Fingerprint Study
    [Collection Tool] and can be generated using the Fingerprint Study
    [Image Builder]. The Collection Tool prompts the test participant to touch
    the sensor and aggregates the fingerprints collected. For qualifications,
    this image must be built by the Chrome OS Fingerprint Team.

    The following are some of the Fingerprint Study Collection Tool
    configuration parameters to consider:

    -   The number of fingers to capture.
    -   The number of enrollment captures.
    -   The number of verification captures.

    See [Image Builder] and [Collection Tool] for instructions on how to prepare
    the image.

4.  A **Biometric Performance Evaluation Tool (BPET)**

    The BPET is a software application that simulates fingerprint matches using
    the raw fingerprint captures, produced by the fingerprint study tool. It is
    expected to accurately evaluate whether the fingerprint system meets the
    [Chrome OS Fingerprint Requirements] for FRR/FAR/SAR, as true to real world
    conditions as possible.

    See [Biometric Performance Evaluation Tool (BPET) Requirements](BPET.md) for
    detailed requirements.

## Process

```
    ┌───────────────┐
    │               │
    │ Image Builder │
    │               │
    └───────┬───────┘
            │
            ▼
┌───────────────────────┌──────────────────────────────────────────────────────┐
│ Collection Chromebook │              Offline Capture Processing              │
│                       │                                                      │
│                       │                                                      │
│  ┌─────────────────┐  │  ┌─────────────────────────────┐   ┌───────────────┐ │
│  │                 │  │  │                             │   │               │ │
│  │ Collection Tool ├──┼─►│ Performance Evaluation Tool ├──►│ Analysis Tool │ │
│  │                 │  │  │                             │   │               │ │
│  └─────────────────┘  │  └─────────────────────────────┘   └───────┬───────┘ │
│           ▲           │                                            │         │
│           │           │                                            ▼         │
│           │           │                                         xxxxxxxxxxxx │
│                       │                                        x          x  │
│    FPMCU Firmware     │                                       x  Report  x   │
│           +           │                                      x          x    │
│  Fingerprint Sensor   │                                     xxxxxxxxxxxx     │
└───────────────────────└──────────────────────────────────────────────────────┘
```

<!-- The ACII art editor project can be found http://shortn/_ueP8GtZdta. -->

1.  Prepare Chromebook for collection with a valid fingerprint sensor, valid
    FPMCU firmware, and the ChromeOS image generated by the [Image Builder].

1.  Capture participant fingerprint samples on the Chromebook, using the onboard
    [Collection Tool].

    *For qualification, the [Fingerprint Sensor FAR/FRR Test Procedure] must be
    followed.*

1.  Process all collected participant fingerprint samples using the
    [Performance Evaluation Tool]. This tool is intended to simulate enrollments
    and matches under multiple testing scenarios/modes.

    *For qualifications, no fingerprint samples may be excluded/filtered. If a
    truly unique and unnatural fingerprint capturing situation arises, the
    Chrome OS Fingerprint Team can assess and correct the discrepancy on a case
    by case basis.*

1.  Run the [Analysis Tool] on the matching results of the Performance
    Evaluation Tool captured fingerprint samples to determine if the fingerprint
    matching performance meets [Chrome OS Fingerprint Requirements].

1.  Further manual testing must be done to ensure that on-chip matching times
    meet [Chrome OS Fingerprint Requirements].

[Analysis Tool]: analysis-tool/
[Image Builder]: fpstudy-image-builder/
[Collection Tool]: collection-tool/
[Performance Evaluation Tool]: BPET.md

<!-- TODO(hesling): The following test procedure needs to be published for all. -->

[Fingerprint Sensor FAR/FRR Test Procedure]: https://chromeos.google.com/partner/dlm/docs/hardware-specs/fingerprintsensor.html
[Chrome OS Fingerprint Requirements]: https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebook.html#fingerprint
