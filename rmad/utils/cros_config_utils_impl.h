// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CROS_CONFIG_UTILS_IMPL_H_
#define RMAD_UTILS_CROS_CONFIG_UTILS_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <chromeos-config/libcros_config/cros_config_interface.h>

#include "rmad/utils/cros_config_utils.h"

namespace rmad {

class CrosConfigUtilsImpl : public CrosConfigUtils {
 public:
  CrosConfigUtilsImpl();
  CrosConfigUtilsImpl(const std::string& config_file_path,
                      std::unique_ptr<brillo::CrosConfigInterface> cros_config);
  ~CrosConfigUtilsImpl() override = default;

  bool GetModelName(std::string* model_name) override;
  bool GetCurrentSkuId(int* sku) override;
  bool GetCurrentWhitelabelTag(std::string* whitelabel_tag) override;
  bool GetSkuIdList(std::vector<int>* sku_list) override;
  bool GetWhitelabelTagList(
      std::vector<std::string>* whitelabel_tag_list) override;

 private:
  bool GetMatchedItemsFromIdentity(const std::string& key,
                                   std::vector<base::Value>* list);

  std::string config_file_path_;
  std::unique_ptr<brillo::CrosConfigInterface> cros_config_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CROS_CONFIG_UTILS_IMPL_H_
