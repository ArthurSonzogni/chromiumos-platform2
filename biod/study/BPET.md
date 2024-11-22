<!-- Format this doc with `mdformat --compatibility --w BPET.md`. -->

# Biometric Performance Evaluation Tool (BPET) Requirements

The BPET is a software application that simulates fingerprint matches using the
raw fingerprint captures, produced by the fingerprint study tool. It is expected
to accurately evaluate whether the fingerprint system meets the
[Chrome OS Fingerprint Requirements] for FRR/FAR/SAR, as true to real world
conditions as possible.

**Key Features:**

1.  Ingests raw captures from the fingerprint study tool
1.  Simulates enrollment and matching under 3 different
    [Testing Modes](#testing-modes)
    -   Template Updating Disabled
    -   Select/Isolated Template Update
    -   Continuous Template Update
1.  Calculates core biometric metrics
    -   False Reject Rate (FRR)
    -   False Accept Rate (FAR)
    -   Spoof Accept Rate (SAR) - when live and spoof data is included. If
        liveness detection is not supported in a BPET tool built for the sensor,
        the SAR measured in this case is the inherent SAR of the regular
        matcher.
1.  Generates performance visualizations
    -   Plot of [Detection Error Tradeoff] \(FAR on x-axis, FRR on y-axis\)
    -   Live Detection Error Tradeoff (LDET) curve: tradeoff between FRR and
        SAR - when spoof data is included
1.  Provides detailed performance reports
    -   Average, per-user, and per-finger error rates
    -   Statistical analysis of error rate distribution
    -   Identification of potential outliers and problematic captures
    -   Identification of any Failure to Enrolls (FTE)
1.  Parallel execution that utilizes all CPU cores for faster processing

## Testing Modes

The following three different testing modes need to be simulated:

-   Template Updating Disabled

    This mode verifies only against the original enrollment samples. This mimics
    the out-of-the-box performance, where the user/adversary is matching against
    only the original enrollment captures, without any prior template updating.

-   Select/Isolated Template Update

    This mode uses a dedicated set of captures that are only for template
    updating. After which, template updating is disabled for the remaining
    verification/evaluation. This assesses the real-world after some amount of
    template updating has already occurred.

-   Continuous Template Updating

    This mode mimics production, where all verification samples yield template
    updating. This assesses the average performance when operating with normal
    production behavior, where the performance may be changing continuously
    (performance may decrease or increase on each verification). Accurate data
    labeling is crucial for reliable FRR, FAR, and SAR results.

## System Requirements

The tool itself must run on a standard amd64 GNU/Linux machine, but must invoke
the same fingerprint matching library with the same parameters to the FPMCU
matching library being qualified. This tool must accurately measure the
performance of the provided FPMCU matching library being qualified. Again, there
should be no difference in performance between the performance evaluation tool
and the fingerprint systems being qualified.

*If the FPMCU fingerprint matching library is provided by the vendor, the vendor
is required to provide the Performance Evaluation Tool. This tool must be
written in Python 3, but may invoke matching specific functions from a
pre-compiled matching library. The Python 3 source must be committed to the
[Chromium OS FP Study Repository]. Google reserves the right to have a third
party auditor evaluate the accuracy of the provided Performance Evaluation Tool
and its accompanying matching library. This includes source-level analysis of
the pre-compiled matching library.*

[Chrome OS Fingerprint Requirements]: https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebook.html#fingerprint
[Chromium OS FP Study Repository]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/biod/study/
[Detection error tradeoff]: https://en.wikipedia.org/wiki/Detection_error_tradeoff
