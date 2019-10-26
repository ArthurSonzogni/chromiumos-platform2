// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_NVME_STORAGE_H_
#define RUNTIME_PROBE_FUNCTIONS_NVME_STORAGE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>

#include "runtime_probe/function_templates/storage.h"

namespace runtime_probe {

class NvmeStorageFunction : public StorageFunction {
 public:
  static constexpr auto function_name = "nvme_storage";
  std::string GetFunctionName() const override { return function_name; }

  static std::unique_ptr<ProbeFunction> FromDictionaryValue(
      const base::DictionaryValue& dict_value) {
    std::unique_ptr<NvmeStorageFunction> instance{new NvmeStorageFunction()};

    if (dict_value.size() != 0) {
      LOG(ERROR) << function_name << " does not take any argument";
      return nullptr;
    }
    return instance;
  }

 protected:
  base::DictionaryValue EvalByDV(
      const base::DictionaryValue& storage_dv) const override {
    return {};
  }
  // Eval the NVMe storage indicated by |node_path| inside the
  // runtime_probe_helper.
  base::DictionaryValue EvalInHelperByPath(
      const base::FilePath& node_path) const override;

 private:
  bool CheckStorageTypeMatch(const base::FilePath& node_path) const;
  std::string GetStorageFwVersion(const base::FilePath& node_path) const;
  static ProbeFunction::Register<NvmeStorageFunction> register_;
};

// Register the NvmeStorageFunction
REGISTER_PROBE_FUNCTION(NvmeStorageFunction);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_NVME_STORAGE_H_
