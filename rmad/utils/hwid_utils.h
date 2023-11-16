// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_HWID_UTILS_H_
#define RMAD_UTILS_HWID_UTILS_H_

#include <optional>
#include <string>

namespace rmad {

struct HwidElements {
  std::optional<std::string> model_name;
  std::optional<std::string> brand_code;
  std::optional<std::string> encoded_components;
  std::optional<std::string> checksum;

  // Used by EXPECT_EQ.
  bool operator==(const HwidElements&) const = default;
};

// |HwidUtils| only support HWID v3.
class HwidUtils {
 public:
  HwidUtils() = default;
  virtual ~HwidUtils() = default;

  // Verify the checksum of the given HWID string. The function will fail if the
  // given HWID string has invalid length or the checksum is not correct.
  virtual bool VerifyChecksum(const std::string& hwid) = 0;

  // Validate the format of the HWID string provided. A valid HWID string should
  // be in the a format like:
  // <MODEL_NAME>-<RLZ_CODE> <(4N+1)CHARS_ENCODED_COMPONENT><(2)CHARS_CHECKSUM>
  // , which:
  // (1) Can be split into exactly 2 parts by whitespace.
  // (2) The format of the first part is <MODEL>[-<BRAND_CODE>].
  // (3) The encoded component part has a valid length (4N+1).
  // (4) The length of the checksum is 2.
  virtual bool VerifyHwidFormat(const std::string& hwid, bool has_checksum) = 0;

  // Decompose the given HWID string and return HwidElements, which contains
  // `model name`, `brand code`, `encoded components`, and `checksum`.
  virtual std::optional<HwidElements> DecomposeHwid(
      const std::string& hwid) = 0;

  // Calculate checksum from the given HWID string without checksum. The
  // original checksum should be stripped before passing the HWID string to the
  // function.
  virtual std::optional<std::string> CalculateChecksum(
      const std::string& hwid) const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_HWID_UTILS_H_
