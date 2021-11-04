// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_CROS_CONFIG_UTILS_H_
#define RMAD_UTILS_FAKE_CROS_CONFIG_UTILS_H_

#include "rmad/utils/cros_config_utils.h"

#include <string>
#include <vector>

namespace rmad {
namespace fake {

class FakeCrosConfigUtils : public CrosConfigUtils {
 public:
  FakeCrosConfigUtils() = default;
  ~FakeCrosConfigUtils() override = default;

  bool GetModelName(std::string* model_name) const override;
  bool GetCurrentSkuId(int* sku) const override;
  bool GetCurrentWhitelabelTag(std::string* whitelabel_tag) const override;
  bool GetSkuIdList(std::vector<int>* sku_list) const override;
  bool GetWhitelabelTagList(
      std::vector<std::string>* whitelabel_tag_list) const override;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_CROS_CONFIG_UTILS_H_
