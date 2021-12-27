// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/ssfc_utils_impl.h"

#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/logging.h>

#include "rmad/utils/cmd_utils_impl.h"

namespace {

constexpr char kSsfcScriptDirPath[] = "/usr/share/cros/rmad/ssfc/";
constexpr char kSsfcScriptPathPostfix[] = "_ssfc.sh";

}  // namespace

namespace rmad {

SsfcUtilsImpl::SsfcUtilsImpl() {
  cmd_utils_ = std::make_unique<CmdUtilsImpl>();
  script_search_path_ = base::FilePath(kSsfcScriptDirPath);
}

SsfcUtilsImpl::SsfcUtilsImpl(std::unique_ptr<CmdUtils> cmd_utils,
                             const std::string& script_search_path)
    : cmd_utils_(std::move(cmd_utils)),
      script_search_path_(script_search_path) {}

bool SsfcUtilsImpl::GetSSFC(const std::string& model,
                            bool* need_to_update,
                            uint32_t* ssfc) const {
  CHECK(need_to_update);
  CHECK(ssfc);

  base::FilePath script_path =
      script_search_path_.AppendASCII(model + kSsfcScriptPathPostfix);
  if (!base::PathExists(script_path)) {
    *need_to_update = false;
    return true;
  }

  std::string result;
  if (!cmd_utils_->GetOutput({script_path.MaybeAsASCII()}, &result)) {
    LOG(ERROR) << "Failed to exec [" << script_path.MaybeAsASCII() << "].";
    return false;
  }

  if (!base::HexStringToUInt(result, ssfc)) {
    LOG(ERROR) << "Failed to parse [" << result << "] to unit32_t.";
    return false;
  }

  *need_to_update = true;
  return true;
}

}  // namespace rmad
