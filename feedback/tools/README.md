# Feedback Utility

## perf\_data\_extract.py

This is a utility script for devs analyzing the performance profile contained
in feedback logs. It can be used to extract the `perf-data` and `perfetto-data`
elements from a feedback logs archive and save the extracted data to files
ready for analysis using pprof or Perfetto.

## Usage

To extract the `perf-data` and `perfetto-data` elements from a system logs
archive `system_logs.zip` and save to `/tmp/feedback_perf.data` and
`/tmp/feedback.perfetto-trace`:

```sh
perf_data_extract.py system_logs.zip /tmp
```

To show the help message, use the `-h` flag:

```sh
perf_data_extract.py -h
```
