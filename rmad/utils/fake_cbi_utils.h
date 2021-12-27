// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FAKE_CBI_UTILS_H_
#define RMAD_UTILS_FAKE_CBI_UTILS_H_

#include "rmad/utils/cbi_utils.h"

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/utils/json_store.h"

namespace rmad {
namespace fake {

class FakeCbiUtils : public CbiUtils {
 public:
  explicit FakeCbiUtils(const base::FilePath& working_dir_path);
  ~FakeCbiUtils() override = default;

  bool GetSku(uint64_t* sku) const override;
  bool GetDramPartNum(std::string* dram_part_num) const override;
  bool GetSSFC(uint32_t* ssfc) const override;
  bool SetSku(uint64_t sku) override;
  bool SetDramPartNum(const std::string& dram_part_num) override;
  bool SetSSFC(uint32_t ssfc) override;

 private:
  base::FilePath working_dir_path_;
  // Use |JsonStore| to read the fake CBI values.
  scoped_refptr<JsonStore> json_store_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_UTILS_FAKE_CBI_UTILS_H_
