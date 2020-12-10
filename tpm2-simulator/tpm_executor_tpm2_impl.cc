// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/logging.h>
#include <linux/vtpm_proxy.h>
#include <tpm2/tpm_simulator.hpp>

#include "tpm2-simulator/tpm_executor_tpm2_impl.h"

namespace {

std::string CommandWithCode(uint32_t code) {
  std::string response;
  response.resize(10);
  unsigned char* buffer = reinterpret_cast<unsigned char*>(response.data());
  tpm2::TPM_ST tag = TPM_ST_NO_SESSIONS;
  tpm2::INT32 size = 10;
  tpm2::UINT32 len = size;
  tpm2::TPMI_ST_COMMAND_TAG_Marshal(&tag, &buffer, &size);
  tpm2::UINT32_Marshal(&len, &buffer, &size);
  tpm2::TPM_CC_Marshal(&code, &buffer, &size);
  return response;
}

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
  LOG(INFO) << "vTPM Initialize.";
}

size_t TpmExecutorTpm2Impl::GetCommandSize(const std::string& command) {
  unsigned char* header =
      reinterpret_cast<unsigned char*>(const_cast<char*>(command.data()));
  int32_t header_size = command.size();
  tpm2::TPMI_ST_COMMAND_TAG tag;
  uint32_t command_size;
  tpm2::TPM_RC rc =
      tpm2::TPMI_ST_COMMAND_TAG_Unmarshal(&tag, &header, &header_size);
  if (rc != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to parse tag";
    return command.size();
  }
  rc = tpm2::UINT32_Unmarshal(&command_size, &header, &header_size);
  if (rc != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to parse size";
    return command.size();
  }
  return command_size;
}

std::string TpmExecutorTpm2Impl::RunCommand(const std::string& command) {
  // TODO(yich): ExecuteCommand would mutate the command buffer, so we created a
  // copy of the input command at here.
  std::string command_copy = command;
  unsigned char* command_ptr =
      reinterpret_cast<unsigned char*>(command_copy.data());
  unsigned char* header = command_ptr;
  int32_t header_size = command.size();
  tpm2::TPMI_ST_COMMAND_TAG tag;
  uint32_t command_size;
  tpm2::TPM_CC command_code = 0;
  tpm2::TPM_RC rc =
      tpm2::TPMI_ST_COMMAND_TAG_Unmarshal(&tag, &header, &header_size);
  if (rc != TPM_RC_SUCCESS) {
    return CommandWithCode(rc);
  }
  rc = tpm2::UINT32_Unmarshal(&command_size, &header, &header_size);
  if (rc != TPM_RC_SUCCESS) {
    return CommandWithCode(rc);
  }
  rc = tpm2::TPM_CC_Unmarshal(&command_code, &header, &header_size);
  if (command_code == TPM2_CC_SET_LOCALITY) {
    // Ignoring TPM2_CC_SET_LOCALITY command.
    return CommandWithCode(TPM_RC_SUCCESS);
  }

  unsigned int response_size;
  unsigned char* response;
  tpm2::ExecuteCommand(command.size(), command_ptr, &response_size, &response);
  return std::string(reinterpret_cast<char*>(response), response_size);
}

}  // namespace tpm2_simulator
