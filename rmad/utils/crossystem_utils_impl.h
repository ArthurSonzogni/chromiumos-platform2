// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CROSSYSTEM_UTILS_IMPL_H_
#define RMAD_UTILS_CROSSYSTEM_UTILS_IMPL_H_

#include "rmad/utils/crossystem_utils.h"

#include <string>

namespace rmad {

class CrosSystemUtilsImpl : public CrosSystemUtils {
 public:
  CrosSystemUtilsImpl() = default;
  ~CrosSystemUtilsImpl() = default;

  bool SetInt(const std::string& key, int value) override;
  bool GetInt(const std::string& key, int* value) const override;
  bool SetString(const std::string& key, const std::string& value) override;
  bool GetString(const std::string& key, std::string* value) const override;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CROSSYSTEM_UTILS_IMPL_H_
