// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CROS_CONFIG_UTILS_H_
#define RMAD_UTILS_CROS_CONFIG_UTILS_H_

#include <string>
#include <vector>

namespace rmad {

class CrosConfigUtils {
 public:
  CrosConfigUtils() = default;
  virtual ~CrosConfigUtils() = default;

  virtual bool GetModelName(std::string* model_name) const = 0;
  virtual bool GetCurrentWhitelabelTag(std::string* whitelabel_tag) const = 0;
  virtual bool GetCurrentSkuId(int* sku_id) const = 0;
  virtual bool GetWhitelabelTagList(
      std::vector<std::string>* whitelabel_tag_list) const = 0;
  virtual bool GetSkuIdList(std::vector<int>* sku_id_list) const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CROS_CONFIG_UTILS_H_
