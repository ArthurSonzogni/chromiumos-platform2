// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_SSFC_UTILS_IMPL_H_
#define RMAD_UTILS_SSFC_UTILS_IMPL_H_

#include "rmad/utils/ssfc_utils.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>

#include "rmad/utils/cmd_utils.h"

namespace rmad {

class SsfcUtilsImpl : public SsfcUtils {
 public:
  SsfcUtilsImpl();
  // Used to inject mocked |cmd_utils_| and |script_search_path_| for testing.
  explicit SsfcUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils,
                         const std::string& script_search_path);
  ~SsfcUtilsImpl() override = default;

  bool GetSSFC(const std::string& model,
               bool* need_to_update,
               uint32_t* ssfc) const override;

 private:
  std::unique_ptr<CmdUtils> cmd_utils_;
  base::FilePath script_search_path_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_SSFC_UTILS_IMPL_H_
