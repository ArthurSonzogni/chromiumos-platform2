# Fingerprint Performance Analysis Tool

This tool is used to asses the performance of a fingerprint system.

It uses matcher decision output from test user fingerprint samples to
statistically determine its FAR and FRR confidence range.

## Notes

*   When using multiple test cases, please keep the Sample IDs reported in each
    test case decisions output consistent with the actual global samples being
    used. This is relevant when you are using different sample ranges for
    enrollment, template updating, or verification across different test cases.
    We still want the combined histograms to be able to compare sample
    performance across all test cases.

## Run The Analysis

```bash
# From the biod/study directory.
./python-venv-setup.sh
. .venv/bin/activate
./analysis-tool/run.py --learn-groups-dir data/orig-data data/decisions analysis
```

## Run Unit Tests

```bash
python -m unittest discover -v -s . -p '*_test.py'
```