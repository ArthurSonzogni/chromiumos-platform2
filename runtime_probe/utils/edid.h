// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_EDID_H_
#define RUNTIME_PROBE_UTILS_EDID_H_

#include <memory>
#include <string>
#include <vector>

namespace runtime_probe {

struct Edid {
  std::string vendor;
  int product_id;
  int width;
  int height;

  static std::unique_ptr<Edid> From(const std::vector<uint8_t>& blob);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_EDID_H_
