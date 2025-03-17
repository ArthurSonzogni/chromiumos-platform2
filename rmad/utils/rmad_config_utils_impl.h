// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_RMAD_CONFIG_UTILS_IMPL_H_
#define RMAD_UTILS_RMAD_CONFIG_UTILS_IMPL_H_

#include <memory>
#include <optional>

#include <base/files/file_path.h>

#include "rmad/rmad_config.pb.h"
#include "rmad/utils/cros_config_utils.h"
#include "rmad/utils/rmad_config_utils.h"

namespace rmad {

class RmadConfigUtilsImpl : public RmadConfigUtils {
 public:
  RmadConfigUtilsImpl();
  explicit RmadConfigUtilsImpl(
      const base::FilePath& config_dir_path,
      std::unique_ptr<CrosConfigUtils> cros_config_utils);

  const std::optional<RmadConfig>& GetConfig() const override;

 private:
  void Initialize();

  std::optional<RmadConfig> rmad_config_;

  base::FilePath config_dir_path_;
  std::unique_ptr<CrosConfigUtils> cros_config_utils_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_RMAD_CONFIG_UTILS_IMPL_H_
