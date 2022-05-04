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
constexpr char kFakeCustomLabelTag[] = "fake_custom_label_1";
const std::vector<int> kFakeSkuIdList = {1, 2, 3, 4};
const std::vector<std::string> kFakeCustomLabelTagList = {
    "fake_custom_label_1", "fake_custom_label_2"};

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

bool FakeCrosConfigUtils::GetCurrentCustomLabelTag(
    std::string* custom_label_tag) const {
  CHECK(custom_label_tag);
  *custom_label_tag = kFakeCustomLabelTag;
  return true;
}

bool FakeCrosConfigUtils::GetSkuIdList(std::vector<int>* sku_id_list) const {
  CHECK(sku_id_list);
  *sku_id_list = kFakeSkuIdList;
  return true;
}

bool FakeCrosConfigUtils::GetCustomLabelTagList(
    std::vector<std::string>* custom_label_tag_list) const {
  CHECK(custom_label_tag_list);
  *custom_label_tag_list = kFakeCustomLabelTagList;
  return true;
}

}  // namespace fake
}  // namespace rmad
