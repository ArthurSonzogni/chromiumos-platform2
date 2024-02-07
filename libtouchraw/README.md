# libtouchraw

C++ library to process HID raw data read from /dev/hidraw*.

## Design Documentation

See the [design doc](http://go/cros-platform-heatmap) and the
[implementation proposal](http://go/cros-heatmap-library).

## Usage

The current supported use case is heatmap data. It helps parse HID packets,
defragment them if needed, and synchronize heatmap data with touch coordinates.

This library provides an interface class, which takes a file path(/dev/hidraw*)
and a pointer to a consumer queue of this library.
