<!-- Format this doc with `mdformat --compatibility --w BPET.md`. -->

# Biometric Performance Evaluation Tool (BPET) Requirements

The BPET is a software application that simulates fingerprint matches using the
raw fingerprint captures, produced by the fingerprint study tool. It is expected
to accurately evaluate whether the fingerprint system meets the
[Chrome OS Fingerprint Requirements] for FRR/FAR/SAR, as true to real world
conditions as possible. In general, it should demonstrate the FRR/FAR/SAR
performance throughout the range of possible matching thresholds and the FRR at
predefined thresholds (corresponding to 1/50k and 1/100k FAR).

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

Considering errors can occur in the fingerprint capture/labeling process,
additional diagnostics should be built into the tool to understand which
participants contribute more negatively to the overall performance. Again, the
tool may indicate which participants/captures are problematic, but may not
exclude these from the overall analysis.

At a minimum, the tool should present the following:

-   Plot of FAR vs. matching threshold (threshold on x-axis)
-   Plot of FRR vs. matching threshold (threshold on x-axis)
-   Plot of [Detection Error Tradeoff] \(FAR on x-axis, FRR on y-axis\)
-   The FRR statistics at 1/50k and 1/100k FAR
-   Any Failure to Enrolls (FTE) that occurred

If enrolled template updating is used, the before and after values/plots must be
provided.

[Chrome OS Fingerprint Requirements]: https://chromeos.google.com/partner/dlm/docs/latest-requirements/chromebook.html#fingerprint
[Chromium OS FP Study Repository]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/biod/study/
[Detection error tradeoff]: https://en.wikipedia.org/wiki/Detection_error_tradeoff
