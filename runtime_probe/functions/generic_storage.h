// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_GENERIC_STORAGE_H_
#define RUNTIME_PROBE_FUNCTIONS_GENERIC_STORAGE_H_

#include <memory>

#include <base/values.h>

#include "runtime_probe/functions/ata_storage.h"
#include "runtime_probe/functions/mmc_storage.h"
#include "runtime_probe/functions/nvme_storage.h"
#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class GenericStorageFunction : public ProbeFunction {
 public:
  NAME_PROBE_FUNCTION("generic_storage");

  template <typename T>
  static std::unique_ptr<T> FromKwargsValue(const base::Value& dict_value) {
    PARSE_BEGIN();
    instance->ata_prober_ = CreateProbeFunction<AtaStorageFunction>(dict_value);
    if (!instance->ata_prober_)
      return nullptr;
    instance->mmc_prober_ = CreateProbeFunction<MmcStorageFunction>(dict_value);
    if (!instance->mmc_prober_)
      return nullptr;
    instance->nvme_prober_ =
        CreateProbeFunction<NvmeStorageFunction>(dict_value);
    if (!instance->nvme_prober_)
      return nullptr;
    PARSE_END();
  }

 private:
  // Use FromKwargsValue to ensure the arg is correctly parsed.
  GenericStorageFunction() = default;
  DataType EvalImpl() const override;

  std::unique_ptr<AtaStorageFunction> ata_prober_;
  std::unique_ptr<MmcStorageFunction> mmc_prober_;
  std::unique_ptr<NvmeStorageFunction> nvme_prober_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_GENERIC_STORAGE_H_
