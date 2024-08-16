# SANE Backend WWCB Tests

Tests to help validate that a SANE backend and scanner pair work with
Chromebooks.

## Usage
```
(root-on-dut) $ sane_backend_wwcb_tests --scanner=<SCANNER>
```

A `<SCANNER>` may be obtained from lorgnette by invoking:
`(root-on-dut) $ lorgnette_cli discover`.


### GoogleTest Options

This command is a GoogleTest binary so it works with all
[GoogleTest](https://google.github.io/googletest/) commandline
`--gtest` flags passed after `--`.

Three particularly useful flags are
[--gtest\_output](https://google.github.io/googletest/advanced.html#generating-a-json-report)
for generating a test report,
[--gtest\_fail_fast](https://google.github.io/googletest/advanced.html#stop-test-execution-upon-first-failure)
for ending a test early, and
[--gtest\_filter](https://google.github.io/googletest/advanced.html#running-a-subset-of-the-tests)
for running a subset of tests.

For example:
```
(root-on-dut) $ sane_backend_wwcb_tests --scanner=pfufs:fi-8040:CPDH009721 -- \
--gtest_fail_fast \
--gtest_output="json:wwcb-test-report_$(date +%s).json" \
--gtest_filter='ScanTestParameters/ScanTest.SinglePage/SourceIsADFFront*'
```

The above command will test the `pfufs:fi-8040:CPDH009721` scanner,
end early on the first test failure, limit testing to tests that match
the `ScanTestParameters/ScanTest.SinglePage/SourceIsADFFront*`
pattern, and finally create a `wwcb-test-report_<epoch seconds>.json`
JSON formatted test report.

One may also run a single test using the `--gtest_filter` option by
fully specifying the test name:

```
(root-on-dut) $ sane_backend_wwcb_tests --scanner=pfufs:fi-8040:CPDH009721 -- \
--gtest_filter='ScanTestParameters/ScanTest.SinglePage/SourceIsADFFrontResolutionIs50ColorModeisLineart'
```

Note: The ScanTest tests are
[parameterized](https://google.github.io/googletest/advanced.html#value-parameterized-tests). Parameterized
GoogleTest tests have names generated at runtime.
