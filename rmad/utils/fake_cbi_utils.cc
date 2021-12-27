// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/fake_cbi_utils.h"

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/constants.h"
#include "rmad/utils/json_store.h"

namespace {

constexpr char kSkuKey[] = "sku";
constexpr char kDramPartNumKey[] = "dram_part_num";
constexpr char kSSFCKey[] = "ssfc";

}  // namespace

namespace rmad {
namespace fake {

FakeCbiUtils::FakeCbiUtils(const base::FilePath& working_dir_path)
    : CbiUtils(), working_dir_path_(working_dir_path) {
  json_store_ = base::MakeRefCounted<JsonStore>(
      working_dir_path_.AppendASCII(kCbiFilePath));
  CHECK(!json_store_->ReadOnly());
}

bool FakeCbiUtils::GetSku(uint64_t* sku) const {
  CHECK(sku);
  int value;
  if (!json_store_->GetValue(kSkuKey, &value)) {
    return false;
  }
  *sku = static_cast<uint64_t>(value);
  return true;
}

bool FakeCbiUtils::GetDramPartNum(std::string* dram_part_num) const {
  CHECK(dram_part_num);
  return json_store_->GetValue(kDramPartNumKey, dram_part_num);
}

bool FakeCbiUtils::GetSSFC(uint32_t* ssfc) const {
  CHECK(ssfc);
  int value;
  if (!json_store_->GetValue(kSSFCKey, &value)) {
    return false;
  }
  *ssfc = static_cast<uint32_t>(value);
  return true;
}

bool FakeCbiUtils::SetSku(uint64_t sku) {
  // We believe that the SKU ID used in fake is small enough, so we directly
  // cast it to int.
  return json_store_->SetValue(kSkuKey, static_cast<int>(sku));
}

bool FakeCbiUtils::SetDramPartNum(const std::string& dram_part_num) {
  return json_store_->SetValue(kDramPartNumKey, dram_part_num);
}
bool FakeCbiUtils::SetSSFC(uint32_t ssfc) {
  // We believe that the SSFC used in fake is small enough, so we directly
  // cast it to int.
  return json_store_->SetValue(kSSFCKey, static_cast<int>(ssfc));
}

}  // namespace fake
}  // namespace rmad
