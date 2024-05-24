// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FINGERPRINT_FP_FRAME_UTILS_H_
#define LIBEC_FINGERPRINT_FP_FRAME_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ec {

/**
 * Specifies the dimensions of a raw fingerprint frame buffer.
 */
struct FpFrameBufferToPGMBufferDim {
  /// The width of the frame (in pixels).
  std::size_t width;
  /// The height of the frame (in pixels).
  std::size_t height;
  /// The number of bits used to represent each pixel in a frame. */
  std::size_t bits_per_pixel;
};

/**
 * Converts a raw fingerprint frame buffer to an ASCII PGM (Portable Gray
 * Map) image format.
 *
 * This function takes a raw fingerprint frame buffer, along with its dimensions
 * and bit depth, and generates a PGM image representation as a string.
 *
 * For more information on the PGM format, refer to:
 * - https://en.wikipedia.org/wiki/Netpbm#File_formats
 * - https://netpbm.sourceforge.net/doc/pgm.html
 *
 * @param buffer The raw fingerprint frame buffer data.
 * @param dims The dimensions of the raw fingerprint frame buffer.
 *
 * @return An `std::optional<std::string>` containing the PGM image data if
 * conversion was successful, or `std::nullopt` if an error occurred during
 * conversion.
 */
std::optional<std::string> FpFrameBufferToPGM(
    const std::vector<std::uint8_t>& buffer, FpFrameBufferToPGMBufferDim dims);

}  // namespace ec

#endif  // LIBEC_FINGERPRINT_FP_FRAME_UTILS_H_
