// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_CBI_UTILS_H_
#define RMAD_UTILS_FAKE_CBI_UTILS_H_

#include "rmad/utils/cbi_utils.h"

#include <map>
#include <string>
#include <vector>

namespace rmad {

class FakeCbiUtils : public CbiUtils {
 public:
  FakeCbiUtils() = default;
  ~FakeCbiUtils() = default;

  bool SetCbi(int tag, const std::string& value, int set_flag) override;
  bool GetCbi(int tag, std::string* value, int get_flag) const override;
  bool SetCbi(int tag, uint64_t value, int size, int set_flag) override;
  bool GetCbi(int tag, uint64_t* value, int get_flag) const override;

 private:
  std::map<int, std::string> cbi_map_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_CBI_UTILS_H_
