// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_REGIONS_UTILS_H_
#define RMAD_UTILS_FAKE_REGIONS_UTILS_H_

#include "rmad/utils/regions_utils.h"

#include <string>
#include <vector>

namespace rmad {
namespace fake {

class FakeRegionsUtils : public RegionsUtils {
 public:
  FakeRegionsUtils() = default;
  ~FakeRegionsUtils() override = default;

  bool GetRegionList(std::vector<std::string>* region_list) const override;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_REGIONS_UTILS_H_
