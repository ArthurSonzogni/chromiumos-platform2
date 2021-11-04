// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_regions_utils.h"

#include <string>
#include <vector>

#include <base/check.h>

namespace {

const std::vector<std::string> kFakeRegionList = {"fake_region_1",
                                                  "fake_region_2"};

}  // namespace

namespace rmad {
namespace fake {

bool FakeRegionsUtils::GetRegionList(
    std::vector<std::string>* region_list) const {
  CHECK(region_list);

  *region_list = kFakeRegionList;
  return true;
}

}  // namespace fake
}  // namespace rmad
