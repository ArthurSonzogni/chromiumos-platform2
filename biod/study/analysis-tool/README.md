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

## Experiment Directory Files

1.  `FAR_decisions.csv` / `FRR_decisions.csv`:

    The FAR_decisions.csv and FRR_decisions.csv use the same CSV format, but
    should contain all matcher imposter (false-accept) and true user
    (false-reject) attempts, respectively.

    Example **FAR_decisions.csv**:

    ```csv
    EnrollUser,EnrollFinger,VerifyUser,VerifyFinger,VerifySample,Decision
    10001,0,10002,0,0,REJECT
    10001,0,10002,0,1,REJECT
    10001,0,10002,0,2,ACCEPT
    ...
    ```

    Example **FRR_decisions.csv**

    ```csv
    EnrollUser,EnrollFinger,VerifyUser,VerifyFinger,VerifySample,Decision
    10001,0,10001,0,0,ACCEPT
    10001,0,10001,0,1,ACCEPT
    10001,0,10001,0,2,REJECT
    ...
    ```

1.  `user_groups.csv`:

    The user_groups.csv file is optional, but should contain the user to group
    mapping, if provided.

    Example:

    ```csv
    User,Group
    10001,A
    10002,B
    10003,C
    10004,D
    10005,E
    10006,F
    ...
    ```

## Examples

```bash
# From the biod/study directory.
./python-venv-setup.sh
. .venv/bin/activate

# You can use defaults, but limiting finger, samples, and users will make the
# analysis run faster. If you provide an non group multiple for users count,
# you can simply disable groups by adding --groups=0.
./analysis-tool/simulate_fpstudy.py simulation --fingers 4 --samples 40 --frr_prob_percent 6
# The next command will launch two plots in your browser, so you need to
# run this in an interactive session.
./analysis-tool/run.py analyze simulation
```
