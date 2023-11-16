// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_HWID_UTILS_IMPL_H_
#define RMAD_UTILS_HWID_UTILS_IMPL_H_

#include <rmad/utils/hwid_utils.h>

#include <string>

namespace rmad {

class HwidUtilsImpl : public HwidUtils {
 public:
  HwidUtilsImpl() = default;
  ~HwidUtilsImpl() override = default;

  bool VerifyChecksum(const std::string& hwid) override;
  bool VerifyHwidFormat(const std::string& hwid, bool has_checksum) override;
  std::optional<HwidElements> DecomposeHwid(const std::string& hwid) override;
  std::optional<std::string> CalculateChecksum(
      const std::string& hwid) const override;
};

}  // namespace rmad

#endif  // RMAD_UTILS_HWID_UTILS_IMPL_H_
