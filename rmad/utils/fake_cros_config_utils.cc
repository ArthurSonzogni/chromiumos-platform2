// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_cros_config_utils.h"

#include <string>
#include <vector>

#include <base/check.h>

namespace {

constexpr char kFakeModelName[] = "fake_model";
constexpr int kFakeSkuId = 1;
constexpr char kFakeWhitelabelTag[] = "fake_whitelabel_1";
const std::vector<int> kFakeSkuIdList = {1, 2, 3, 4};
const std::vector<std::string> kFakeWhitelabelTagList = {"fake_whitelabel_1",
                                                         "fake_whitelabel_2"};

}  // namespace

namespace rmad {
namespace fake {

bool FakeCrosConfigUtils::GetModelName(std::string* model_name) const {
  CHECK(model_name);
  *model_name = kFakeModelName;
  return true;
}

bool FakeCrosConfigUtils::GetCurrentSkuId(int* sku_id) const {
  CHECK(sku_id);
  *sku_id = kFakeSkuId;
  return true;
}

bool FakeCrosConfigUtils::GetCurrentWhitelabelTag(
    std::string* whitelabel_tag) const {
  CHECK(whitelabel_tag);
  *whitelabel_tag = kFakeWhitelabelTag;
  return true;
}

bool FakeCrosConfigUtils::GetSkuIdList(std::vector<int>* sku_id_list) const {
  CHECK(sku_id_list);
  *sku_id_list = kFakeSkuIdList;
  return true;
}

bool FakeCrosConfigUtils::GetWhitelabelTagList(
    std::vector<std::string>* whitelabel_tag_list) const {
  CHECK(whitelabel_tag_list);
  *whitelabel_tag_list = kFakeWhitelabelTagList;
  return true;
}

}  // namespace fake
}  // namespace rmad
