// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_VPD_UTILS_H_
#define RMAD_UTILS_FAKE_VPD_UTILS_H_

#include "rmad/utils/vpd_utils.h"

#include <map>
#include <string>

namespace rmad {

class FakeVpdUtils : public VpdUtils {
 public:
  FakeVpdUtils() = default;
  ~FakeVpdUtils() = default;

  bool SetRoVpd(const std::string& key, const std::string& value) override;
  bool GetRoVpd(const std::string& key, std::string* value) const override;
  bool SetRwVpd(const std::string& key, const std::string& value) override;
  bool GetRwVpd(const std::string& key, std::string* value) const override;

 private:
  std::map<std::string, std::string> ro_map_;
  std::map<std::string, std::string> rw_map_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_VPD_UTILS_H_
