<!-- Format this doc with `mdformat --compatibility --w ANALYSIS.md`. -->

# Fingerprint System Performance Analysis and Qualification

This document gives a high level overview of what is required to analyze the
performance of a fingerprint system for Chrome OS. In this document, the
*fingerprint system* is the combination of the fingerprint sensor, fingerprint
MCU (FPMCU), and matching library that runs on the FPMCU.

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
    Chromebook being tested. In particular, this image must include the
    [Fingerprint Study Tool]. This tool prompts the test participant to touch
    the sensor and aggregates the fingerprints collected. For qualifications,
    this image must be built by the Chrome OS Fingerprint Team.

    The following are some of the Fingerprint Study Tool configuration
    parameters to consider:

    -   The number of fingers to capture.
    -   The number of enrollment captures.
    -   The number of verification captures.

    See [Fingerprint Study Tool] for instructions on how to prepare the image.

4.  A **Biometric Performance Evaluation Tool (BPET)**

    The BPET is a software application that simulates fingerprint matches using
    the raw fingerprint captures, produced by the fingerprint study tool. It is
    expected to accurately evaluate whether the fingerprint system meets the
    [Chrome OS Fingerprint Requirements] for FRR/FAR/SAR, as true to real world
    conditions as possible.

    See [Biometric Performance Evaluation Tool (BPET) Requirements](BPET.md) for
    detailed requirements.

## Process

1.  Capture participant fingerprint samples using the [Fingerprint Study Tool].

    For qualification, the [Fingerprint Sensor FAR/FRR Test Procedure] must be
    followed.

2.  Run the analysis tool on the captured fingerprint samples to determine if
    the fingerprint matching performance meets
    [Chrome OS Fingerprint Requirements].

    For qualifications, no fingerprint samples may be excluded/filtered. If a
    truly unique and unnatural fingerprint capturing situation arises, the
    Chrome OS Fingerprint Team can assess and correct the discrepancy on a case
    by case basis.

3.  Further manual testing must be done to ensure that on-chip matching times
    meet [Chrome OS Fingerprint Requirements].

[Fingerprint Study Tool]: README.md

<!-- TODO(hesling): The following test procedure needs to be published for all. -->

[Fingerprint Sensor FAR/FRR Test Procedure]: https://chromeos.google.com/partner/dlm/docs/hardware-specs/fingerprintsensor.html
[Chrome OS Fingerprint Requirements]: https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebook.html#fingerprint
