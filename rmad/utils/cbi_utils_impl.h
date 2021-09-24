// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_CBI_UTILS_IMPL_H_
#define RMAD_UTILS_CBI_UTILS_IMPL_H_

#include "rmad/utils/cbi_utils.h"

#include <string>
#include <vector>

namespace rmad {

// Calls `ectool` command to set/get CBI values.

class CbiUtilsImpl : public CbiUtils {
 public:
  CbiUtilsImpl() = default;
  ~CbiUtilsImpl() = default;

  bool GetSku(uint64_t* sku) const override;
  bool GetDramPartNum(std::string* dram_part_num) const override;
  bool SetSku(uint64_t sku) override;
  bool SetDramPartNum(const std::string& dram_part_num) override;

 protected:
  bool SetCbi(int tag, const std::string& value, int set_flag = 0) override;
  bool GetCbi(int tag, std::string* value, int get_flag = 0) const override;
  bool SetCbi(int tag, uint64_t value, int size, int set_flag = 0) override;
  bool GetCbi(int tag, uint64_t* value, int get_flag = 0) const override;
};

}  // namespace rmad

#endif  // RMAD_UTILS_CBI_UTILS_IMPL_H_
