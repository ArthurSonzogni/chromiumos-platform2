# Chromium OS Structured Metrics

The Chromium OS "libstructuredmetrics" package is a lightweight library for
clients to record Structured metrics.

The events will be stored in a file and a browser Chromium process will read
these files to be uploaded.

There is currently no way for events to be uploaded independent of Chromium.

## Using Structured Metrics

TODO(jongahn): Write a How-to-use section.

## Lib Directory

The `lib` directory is synced manually from Chromium directory
[//components/metrics/structured/lib](https://source.chromium.org/chromium/chromium/src/+/main:components/metrics/structured/lib/).
using a tool called [Copybara](https://github.com/google/copybara).

The Copybara script that syncs this library is located in Google's internal
repository. Please check
//googleclient/chrome/crosdata/structured_metrics/libshare/README.md for more
details.

## Sync Directory

The sync directory is used to sync files to both Chromium and internal google
repositories. This directory contains scripts and files for parsing and
interpreting events declared in the file `structured.xml`.
