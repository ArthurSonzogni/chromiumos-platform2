# pmt_tool

A command-line utility for sampling sampling and decoding of the [Intel PMT]
data via libpmt to a user friendly format on stdout.

[Intel PMT]: https://github.com/intel/Intel-PMT

## HOWTO

### Run

`pmt_tool` is a command-line utility that should be run from a shell on a
ChromeOS device. It samples and decodes Intel PMT data, printing it to
standard output. To save the decoded data, stdout can be redirected to a custom file.

Basic usage:
```
pmt_tool [options]
```

### Options

*   `-i`: Seconds to wait between samples.
*   `-t`: Run sampling for the specified number of seconds.
*   `-n`: Number of samples to capture. Default is 0 (run until
    interrupted).
*   `-f`: Output format. Supported values: `csv`, `raw`, `debug`.
    Default is `raw`.

Note that -t and -n flags are mutually exclusive.

### Examples

Capture 10 samples with 1 second interval in CSV format:
```
pmt_tool -n=10 -i=1 -f=csv > /tmp/pmt.csv
```

Use `--help` to see the full list of available options.
