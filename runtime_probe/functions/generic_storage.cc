// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/generic_storage.h"
#include <memory>

namespace runtime_probe {

std::unique_ptr<GenericStorageFunction> GenericStorageFunction::FromKwargsValue(
    const base::Value& dict_value) {
  PARSE_BEGIN(GenericStorageFunction);
  instance->ata_prober_ = AtaStorageFunction::FromKwargsValue(dict_value);
  if (!instance->ata_prober_)
    return nullptr;
  instance->mmc_prober_ = MmcStorageFunction::FromKwargsValue(dict_value);
  if (!instance->mmc_prober_)
    return nullptr;
  instance->nvme_prober_ = NvmeStorageFunction::FromKwargsValue(dict_value);
  if (!instance->nvme_prober_)
    return nullptr;
  PARSE_END();
}

base::Optional<base::Value> GenericStorageFunction::EvalByDV(
    const base::Value& storage_dv) const {
  auto* storage_type = storage_dv.FindStringKey("type");

  if (!storage_type) {
    LOG(ERROR) << "No \"type\" field in current storage dictionary Value.";
    return base::nullopt;
  }
  if (*storage_type == "ATA")
    return ata_prober_->EvalByDV(storage_dv);
  if (*storage_type == "MMC")
    return mmc_prober_->EvalByDV(storage_dv);
  if (*storage_type == "NVMe")
    return nvme_prober_->EvalByDV(storage_dv);
  LOG(WARNING) << "Type \"" << *storage_type << "\" not recognized";
  return base::nullopt;
}

base::Optional<base::Value> GenericStorageFunction::EvalInHelperByPath(
    const base::FilePath& node_path) const {
  VLOG(1) << "Trying to determine the type of storage device \""
          << node_path.value() << "\"";

  auto storage_res = ata_prober_->EvalInHelperByPath(node_path);
  if (!storage_res)
    storage_res = mmc_prober_->EvalInHelperByPath(node_path);
  if (!storage_res)
    storage_res = nvme_prober_->EvalInHelperByPath(node_path);
  return storage_res;
}
}  // namespace runtime_probe
