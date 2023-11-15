// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_HWID_UTILS_H_
#define RMAD_UTILS_HWID_UTILS_H_

#include <optional>
#include <string>

namespace rmad {

// |HwidUtils| only support HWID v3.
class HwidUtils {
 public:
  HwidUtils() = default;
  virtual ~HwidUtils() = default;

  // Verify the checksum of the given HWID string. The function will fail if the
  // given HWID string has invalid length or the checksum is not correct.
  virtual bool VerifyChecksum(const std::string& hwid) = 0;

 private:
  // Calculate checksum from the given HWID string without checksum. The
  // original checksum should be stripped before passing the HWID string to the
  // function.
  virtual std::optional<std::string> CalculateChecksum(
      const std::string& hwid) const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_HWID_UTILS_H_
