# intel_pmt_plugin

## Overview

`intel_pmt_plugin` is an external collector plugin for Netdata. It retrieves
telemetry data from Intel Platform Monitoring Technology (PMT) hardware
capabilities and displays it on Netdata charts.

The plugin primarily parses periodic raw PMT data that is being sampled by heartd
and decodes it using `libpmt`. Alternatively, it can interface with the ChromiumOS
`pmt_tool` to sample data if heartd is not configured by the user.

This plugin exposes these metrics to Netdata for real-time monitoring and
visualization through the [Netdata External Plugins API].

[Netdata External Plugin API]: https://learn.netdata.cloud/docs/developer-and-contributor-corner/external-plugins

## Prerequisites

To use this plugin, the target system must meet the following requirements:

1.  **Hardware**: An Intel platform that supports Intel PMT (e.g., Meteor Lake).
2.  **Kernel**: A Linux kernel with Intel PMT drivers enabled (e.g.,
    `intel_pmt_class`).
3.  **Software**:
    *   `heartd`: (Recommended) System service for periodic health monitoring.
    *   `pmt_tool`: Utility for on-demand sampling of Intel PMT data,
        used as a fallback.
    *   `netdata`: The monitoring agent.

## Features

*   **Periodic Ingestion**: Ingests raw PMT data in real time as it is being
    sampled by `heartd` periodically.
*   **Libmpt Integration**: Decodes raw data sampled by `heartd` using
    `libpmt/decoder`.
*   **Fallback Mode**: Can invoke `pmt_tool` to sample data if `heartd` is
    unavailable.
*   **Dynamic Charts**: Creates charts dynamically based on the telemetry
    regions and fields exposed by the hardware.
*   **Low Overhead**: Efficiently processes data to minimize system impact.

## Installation

In a standard ChromiumOS build environment, this plugin is typically installed
alongside `pmt_tool`.

To manually install or enable it for Netdata:

1.  Ensure the plugin script/binary is executable:
    ```
    chmod +x intel_pmt.plugin
    ```

2.  Place the plugin in the Netdata plugins directory (typically
    `/usr/libexec/netdata/plugins.d/` or
    `/usr/local/etc/netdata/custom-plugins.d/`).

3.  Ensure the `netdata` user has permissions to execute `pmt_tool` and access
    the necessary system files (usually under `/sys/class/intel_pmt`).
    For example, run netdata as root: `netdata -u root` or `start_netdata.sh`.

## Configuration

### Command-line Options

The plugin supports the following command-line options to configure the data
source:

*   `--source`: Specifies the data source format.
    *   `heartd`: Reads periodic raw data sampled by the `heartd` service. This
        is the recommended mode.
    *   `csv`: Reads data from a CSV file or invokes `pmt_tool` to generate it.
        (Default)

The following options are only applicable if `--source=csv`.
*   `--path`: Specifies the path to the input file (e.g., CSV file).
*   `--seconds`: Sampling interval in seconds when using `pmt_tool`.
*   `--records`: Number of records to collect when using `pmt_tool`.

### Enabling the Plugin

Netdata generally auto-detects external plugins found in its plugins directory.
To explicitly enable or disable it, edit `/usr/local/etc/netdata/netdata.conf`:

```
[plugins]
    intel_pmt_plugin = yes
```
