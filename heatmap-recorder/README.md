# heatmap-recorder

## Introduction

This repository contains a tool to help record heatmap over spi at the HID
protocol level.

## Setting up

Make sure that your device has a dev or test image. You can run this tool using
the `heatmap-recorder` command by entering the [command prompt].

## Commands

`heatmap-recorder` supports two formats: `human readable format` and `binary
format`. And four options: `--decode`, `--full`, `--filter`, and `--skip`.

This tool takes a device path /dev/hidraw* as an argument or lists available
hidraw devices for users to choose.

### human readable format

This format prints out the heatmap data in human-readable format(default
option).

example to take path as an argument:
```sh
$ heatmap-recorder --path=/dev/hidraw0
```
example to list available paths:
```sh
$ heatmap-recorder
Available devices:
/dev/hidraw0    spi 04F3:4222
/dev/hidraw1    Wacom Co.,Ltd. Wacom One Pen Display 13
Select the device event number [0-1]:
```

#### --decode

This option decodes the heatmap data if it is encoded.

example:
```sh
$ heatmap-recorder --decode
```

#### --full

This option prints out the full frame of heatmap data. By default only the first
and last five rows are dumped.

example:
```sh
$ heatmap-recorder --full
```

#### --filter

This option filters out heatmap values within a threshold.

example with a threshold 166:
```sh
$ heatmap-recorder --filter=166
```

#### --skip

This option skips heatmap frames that are all zeros. Normally this option is
used together with option `filter` to take effect.

example:
```sh
$ heatmap-recorder --filter=166 --skip`
```

### binary format

This format prints out the heatmap data in binary format. When this format is
requested, `decode` and `full` options are automatically enabled. `filter` and
`skip` options can also be enabled if specified.

example:
```sh
$ heatmap-recorder --binary
```

### Debug

This tool also supports debug options.

example to set log level to ERROR, default is WARNING:
```sh
$ heatmap-recorder --log_level=2
```
example to print help message:
```sh
$ heatmap-recorder --help
```
