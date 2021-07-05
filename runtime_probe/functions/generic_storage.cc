// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iterator>

#include "runtime_probe/functions/generic_storage.h"

namespace runtime_probe {
namespace {

void ConcatenateDataType(GenericStorageFunction::DataType* dest,
                         GenericStorageFunction::DataType&& src) {
  std::move(src.begin(), src.end(), std::back_inserter(*dest));
}
}  // namespace

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

GenericStorageFunction::DataType GenericStorageFunction::EvalImpl() const {
  DataType result{};
  ConcatenateDataType(&result, ata_prober_->Eval());
  ConcatenateDataType(&result, mmc_prober_->Eval());
  ConcatenateDataType(&result, nvme_prober_->Eval());
  return result;
}

}  // namespace runtime_probe
