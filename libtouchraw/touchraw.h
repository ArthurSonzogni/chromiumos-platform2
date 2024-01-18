// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTOUCHRAW_TOUCHRAW_H_
#define LIBTOUCHRAW_TOUCHRAW_H_

#include <cstdint>
#include <optional>
#include <span>

namespace touchraw {

// Describe HID data received from the device.
struct HIDData {
  uint8_t report_id;
  std::span<const uint8_t> payload;
};

enum ReportType {
  kInvalid = 0,
  kFirst,
  kSubsequent,
};

// Describe a chunk of heatmap data from the input device.
// Please refer to go/cros-heatmap-external HID Descriptor Format for
// definitions.
struct HeatmapChunk {
  // Please refer to HID Usage Tables
  // https://www.usb.org/sites/default/files/hut1_4.pdf section 16.9.
  uint16_t vendor_id;
  uint16_t protocol_version;
  // Please refer to HID Usage Tables
  // https://www.usb.org/sites/default/files/hut1_4.pdf section 16.5.
  uint32_t scan_time;
  // Please refer to HID Usage Tables
  // https://www.usb.org/sites/default/files/hut1_4.pdf section 4.6.
  std::optional<uint32_t> byte_count;
  // Please refer to HID Usage Tables
  // https://www.usb.org/sites/default/files/hut1_4.pdf section 9.2.
  std::optional<uint16_t> sequence_id;
  ReportType report_type;
  // Please refer to HID Usage Tables
  // https://www.usb.org/sites/default/files/hut1_4.pdf section 16.9 Capacitive
  // Heat Map Frame Data.
  std::span<const uint8_t> payload;
};

enum EncodingType {
  kRawADC = 0,
  kDiffData,
  kRLE,
  kZeroRLE,
  kQuantizedRLE,
  kThresholdRLE,
};

// Describes one frame of heatmap data.
// Please refer to go/cros-heatmap-external Heatmap Data Format for details.
struct Heatmap {
  uint16_t vendor_id;
  uint16_t protocol_version;
  uint32_t scan_time;
  EncodingType encoding;
  uint8_t bit_depth;
  uint8_t height;
  uint8_t width;
  uint16_t threshold;
  uint16_t length;
  std::span<const uint8_t> payload;
};

}  // namespace touchraw

#endif  // LIBTOUCHRAW_TOUCHRAW_H_
