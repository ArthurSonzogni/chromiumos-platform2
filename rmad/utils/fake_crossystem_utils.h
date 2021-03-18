// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_CROSSYSTEM_UTILS_H_
#define RMAD_UTILS_FAKE_CROSSYSTEM_UTILS_H_

#include "rmad/utils/crossystem_utils.h"

#include <map>
#include <string>

namespace rmad {

class FakeCrosSystemUtils : public CrosSystemUtils {
 public:
  FakeCrosSystemUtils() = default;
  ~FakeCrosSystemUtils() = default;

  bool SetInt(const std::string& key, int value) override;
  bool GetInt(const std::string& key, int* value) const override;
  bool SetString(const std::string& key, const std::string& value) override;
  bool GetString(const std::string& key, std::string* value) const override;

 private:
  std::map<std::string, int> int_map_;
  std::map<std::string, std::string> str_map_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_CROSSYSTEM_UTILS_H_
