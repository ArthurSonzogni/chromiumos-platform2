// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_frame_utils.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
namespace ec {
namespace {

/**
 * A hand drawn 8 bit image of a line.
 */
std::vector<std::vector<std::uint8_t>> kLine8Bit{
    {255, 255, 255, 255, 255, 255},
    {255, 0, 4, 4, 0, 255},
    {255, 255, 255, 255, 255, 255},
};

constexpr char kLine8BitPGM[] =
    "P2\n"
    "# Sensor is 6x3 8bpp\n"
    "6 3\n"
    "# Max Value:\n"
    "255\n"
    "# Pixels:\n"
    "255 255 255 255 255 255\n"
    "255 0 4 4 0 255\n"
    "255 255 255 255 255 255\n"
    "# END OF FILE\n";

/**
 * A hand drawn 16 bit image of a line.
 */
std::vector<std::vector<std::uint16_t>> kLine16Bit{
    {65535, 65535, 65535, 65535, 65535, 65535},
    {65535, 0, 256, 256, 0, 65535},
    {65535, 65535, 65535, 65535, 65535, 65535},
};

constexpr char kLine16BitPGM[] =
    "P2\n"
    "# Sensor is 6x3 16bpp\n"
    "6 3\n"
    "# Max Value:\n"
    "65535\n"
    "# Pixels:\n"
    "65535 65535 65535 65535 65535 65535\n"
    "65535 0 256 256 0 65535\n"
    "65535 65535 65535 65535 65535 65535\n"
    "# END OF FILE\n";

TEST(FpFrameBufferToPGM, Test8BitFrame) {
  // Serialize the vec<vec<uint8_t>> to vec<uint8_t> flat_buffer.
  std::vector<std::uint8_t> flat_buffer;
  flat_buffer.reserve(kLine8Bit.size() * kLine8Bit[0].size());
  for (const auto& row : kLine8Bit) {
    flat_buffer.insert(flat_buffer.end(), row.begin(), row.end());
  }

  std::optional<std::string> pgm = FpFrameBufferToPGM(
      flat_buffer, {.width = 6, .height = 3, .bits_per_pixel = 8});

  EXPECT_TRUE(pgm);
  EXPECT_THAT(pgm, std::string(kLine8BitPGM));
}

TEST(FpFrameBufferToPGM, Test16BitFrame) {
  // Serialize the vec<vec<uint16_t>> to vec<uint8_t> flat_buffer.
  std::vector<std::uint8_t> flat_buffer;
  flat_buffer.reserve(kLine16Bit.size() * kLine16Bit[0].size());
  for (const auto& row : kLine16Bit) {
    for (const uint16_t& value : row) {
      flat_buffer.push_back(value & 0xFF);
      flat_buffer.push_back(value >> 8);
    }
  }

  std::optional<std::string> pgm = FpFrameBufferToPGM(
      flat_buffer, {.width = 6, .height = 3, .bits_per_pixel = 16});

  EXPECT_TRUE(pgm);
  EXPECT_THAT(pgm, std::string(kLine16BitPGM));
}

TEST(FpFrameBufferToPGM, TestSizeMismatchFails) {
  // Emulate a 2x2 9 bit frame that is too small by 1 byte.
  std::vector<std::uint8_t> flat_buffer(7, 0);

  EXPECT_FALSE(FpFrameBufferToPGM(
      flat_buffer, {.width = 2, .height = 2, .bits_per_pixel = 9}));
}

TEST(FpFrameBufferToPGM, TestBPPIs0Fails) {
  std::vector<std::uint8_t> flat_buffer(8, 0);

  EXPECT_FALSE(FpFrameBufferToPGM(
      flat_buffer, {.width = 2, .height = 2, .bits_per_pixel = 0}));
}

TEST(FpFrameBufferToPGM, TestBPPIs17Fails) {
  std::vector<std::uint8_t> flat_buffer(2 * 2 * 3, 0);

  EXPECT_FALSE(FpFrameBufferToPGM(
      flat_buffer, {.width = 2, .height = 2, .bits_per_pixel = 17}));
}

TEST(FpFrameBufferToPGM, TestWidthIs0Fails) {
  std::vector<std::uint8_t> flat_buffer(8, 0);

  EXPECT_FALSE(FpFrameBufferToPGM(
      flat_buffer, {.width = 0, .height = 2, .bits_per_pixel = 8}));
}

TEST(FpFrameBufferToPGM, TestHeightIs0Fails) {
  std::vector<std::uint8_t> flat_buffer(8, 0);

  EXPECT_FALSE(FpFrameBufferToPGM(
      flat_buffer, {.width = 2, .height = 0, .bits_per_pixel = 8}));
}

}  // namespace
}  // namespace ec
