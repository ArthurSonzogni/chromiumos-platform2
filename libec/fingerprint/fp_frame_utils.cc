// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_frame_utils.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <absl/strings/str_format.h>
#include <base/logging.h>

namespace ec {
namespace {

/**
 * Performs ceiling division of two unsigned integers.
 *
 * This function calculates the ceiling division (rounded-up integer division)
 * of two unsigned integer-like values.
 *
 * @tparam T The unsigned integer-like type (e.g., unsigned int, unsigned long).
 * @param numerator The division numerator.
 * @param denominator The division denominator.
 * @return The ceiling numerator/denominator.
 */
template <typename T>
constexpr T DivideCeiling(T numerator, T denominator) {
  static_assert(std::is_unsigned_v<T>,
                "DivideCeiling requires unsigned integer-like types.");
  return (numerator + denominator - 1) / denominator;
}

}  // namespace

std::optional<std::string> FpFrameBufferToPGM(
    const std::vector<std::uint8_t>& buffer, FpFrameBufferToPGMBufferDim dims) {
  if (dims.width == 0 || dims.height == 0) {
    LOG(ERROR) << "The width and/or height are 0.";
    return std::nullopt;
  }

  // The max supported PGM pixel depth is 16 bit.
  if (dims.bits_per_pixel < 1 || dims.bits_per_pixel > 16) {
    LOG(ERROR) << "Invalid bits per pixel " << dims.bits_per_pixel;
    return std::nullopt;
  }

  std::size_t bytes_per_pixel =
      DivideCeiling(dims.bits_per_pixel, static_cast<std::size_t>(8u));
  std::size_t expected_buffer_size = dims.width * dims.height * bytes_per_pixel;
  if (buffer.size() != expected_buffer_size) {
    LOG(ERROR) << "Buffer size mismatch. Expected: " << expected_buffer_size
               << ", Actual: " << buffer.size();
    return std::nullopt;
  }

  std::string pgm;
  auto it = buffer.begin();

  // Write the graymap PGM ASCII header.
  pgm += "P2\n";
  pgm += absl::StrFormat("# Sensor is %zux%zx %zubpp\n", dims.width,
                         dims.height, dims.bits_per_pixel);
  pgm += absl::StrFormat("%zu %zx\n", dims.width, dims.height);
  pgm += "# Max Value:\n";
  std::size_t pixel_max_value = (1u << dims.bits_per_pixel) - 1;
  assert(0 < pixel_max_value && pixel_max_value < 65536);
  pgm += absl::StrFormat("%zu\n", pixel_max_value);

  pgm += "# Pixels:\n";
  for (std::size_t y = 0; y < dims.height; ++y) {
    for (std::size_t x = 0; x < dims.width; ++x) {
      uint16_t pixel = *it++;
      if (bytes_per_pixel == 2) {
        uint16_t pixel_msb = *it++;
        pixel |= pixel_msb << 8;
      }
      const char* space_sep = x > 0 ? " " : "";
      pgm += absl::StrFormat("%s%u", space_sep, pixel);
    }
    pgm += "\n";
  }
  pgm += "# END OF FILE\n";
  return pgm;
}

}  // namespace ec
