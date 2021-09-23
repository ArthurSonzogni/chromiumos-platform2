// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/userdb_utils.h>
#include <linux/vtpm_proxy.h>
#include <tpm2/tpm_simulator.hpp>

#include "tpm2-simulator/tpm_command_utils.h"
#include "tpm2-simulator/tpm_executor_tpm2_impl.h"

namespace {

constexpr char kSimulatorUser[] = "tpm2-simulator";
constexpr char kNVChipPath[] = "NVChip";

}  // namespace

namespace tpm2_simulator {

void TpmExecutorTpm2Impl::InitializeVTPM() {
  // Initialize TPM.
  tpm2::_plat__Signal_PowerOn();
  /*
   * Make sure NV RAM metadata is initialized, needed to check
   * manufactured status. This is a speculative call which will have to
   * be repeated in case the TPM has not been through the manufacturing
   * sequence yet. No harm in calling it twice in that case.
   */
  tpm2::_TPM_Init();
  tpm2::_plat__SetNvAvail();

  if (!tpm2::tpm_manufactured()) {
    tpm2::TPM_Manufacture(true);
    // TODO(b/132145000): Verify if the second call to _TPM_Init() is necessary.
    tpm2::_TPM_Init();
    if (!tpm2::tpm_endorse())
      LOG(ERROR) << __func__ << " Failed to endorse TPM with a fixed key.";
  }

  uid_t uid;
  gid_t gid;
  if (!brillo::userdb::GetUserInfo(kSimulatorUser, &uid, &gid)) {
    LOG(ERROR) << "Failed to lookup the user name.";
    return;
  }
  if (HANDLE_EINTR(chown(kNVChipPath, uid, gid)) < 0) {
    PLOG(ERROR) << "Failed to chown the NVChip.";
    return;
  }

  LOG(INFO) << "vTPM Initialize.";
}

size_t TpmExecutorTpm2Impl::GetCommandSize(const std::string& command) {
  uint32_t size;
  if (!ExtractCommandSize(command, &size)) {
    LOG(ERROR) << "Command too small.";
    return command.size();
  }
  return size;
}

std::string TpmExecutorTpm2Impl::RunCommand(const std::string& command) {
  // TODO(yich): ExecuteCommand would mutate the command buffer, so we created a
  // copy of the input command at here.
  std::string command_copy = command;
  unsigned char* command_ptr =
      reinterpret_cast<unsigned char*>(command_copy.data());

  CommandHeader header;
  if (!ExtractCommandHeader(command, &header)) {
    LOG(ERROR) << "Command too small.";
    return CreateCommandWithCode(TPM_RC_SUCCESS);
  }

  if (header.code == TPM2_CC_SET_LOCALITY) {
    // Ignoring TPM2_CC_SET_LOCALITY command.
    return CreateCommandWithCode(TPM_RC_SUCCESS);
  }

  unsigned int response_size;
  unsigned char* response;
  tpm2::ExecuteCommand(command.size(), command_ptr, &response_size, &response);
  return std::string(reinterpret_cast<char*>(response), response_size);
}

}  // namespace tpm2_simulator
