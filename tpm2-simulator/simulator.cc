// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/callback.h>
#include <base/files/file_util.h>
#include <base/files/file.h>
#include <base/hash/sha1.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <crypto/sha2.h>
#include <fcntl.h>
#include <linux/vtpm_proxy.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sysexits.h>
#include <tpm2/tpm_simulator.hpp>
#include <unistd.h>

#include "tpm2-simulator/simulator.h"

namespace {

constexpr char kVtpmxPath[] = "/dev/vtpmx";
constexpr size_t kMaxCommandSize = MAX_COMMAND_SIZE;
constexpr size_t kHeaderSize = 10;
constexpr unsigned char kStartupCmd[] = {
    0x80, 0x01,             /* TPM_ST_NO_SESSIONS */
    0x00, 0x00, 0x00, 0x0c, /* commandSize = 12 */
    0x00, 0x00, 0x01, 0x44, /* TPM_CC_Startup */
    0x00, 0x00              /* TPM_SU_CLEAR */
};

// Resizes extend_data to size crypto::kSHA256Length and uses the result to
// extend the indicated PCR.
void ExtendPcr(unsigned int pcr_index, const std::string& extend_data) {
  std::string mode_digest = extend_data;
  mode_digest.resize(crypto::kSHA256Length);
  tpm2::extend_pcr(pcr_index, mode_digest.data());
}

// According to the specified boot mode, extends PCR0 as cr50 does.
// It should only be called once after the PCR0 value is set to all 0s
// (e.g. running Startup with Clear). Calling it twice without resetting the PCR
// will leave the TPM in an unknown boot mode.
//  - developer_mode: 1 if in developer mode, 0 otherwise,
//  - recovery_mode: 1 if in recovery mode, 0 otherwise,
//  - verified_firmware: 1 if verified firmware, 0 if developer firmware.
void ExtendPcr0BootMode(const char developer_mode,
                        const char recovery_mode,
                        const char verified_firmware) {
  const std::string mode({developer_mode, recovery_mode, verified_firmware});
  ExtendPcr(/*pcr_index=*/0, base::SHA1HashString(mode));
}

base::ScopedFD RegisterVTPM() {
  struct vtpm_proxy_new_dev new_dev = {};
  new_dev.flags = VTPM_PROXY_FLAG_TPM2;
  base::ScopedFD vtpmx_fd(HANDLE_EINTR(open(kVtpmxPath, O_RDWR | O_CLOEXEC)));
  if (!vtpmx_fd.is_valid()) {
    return vtpmx_fd;
  }
  if (ioctl(vtpmx_fd.get(), VTPM_PROXY_IOC_NEW_DEV, &new_dev) < 0) {
    PLOG(ERROR) << "Create vTPM failed.";
    // return an invalid FD.
    return {};
  }
  LOG(INFO) << "Create TPM at: /dev/tpm" << new_dev.tpm_num;
  return base::ScopedFD(new_dev.fd);
}

void InitializeVTPM() {
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

  // Send TPM2_Startup(TPM_SU_CLEAR), ignore the result. This is normally done
  // by firmware. Without TPM2_Startup, TpmUtility::CheckState() fails,
  // ResourceManager aborts initialization, and trunks daemon dies.
  unsigned int response_size;
  unsigned char* response;
  unsigned char startup_cmd[sizeof(kStartupCmd)];
  memcpy(startup_cmd, kStartupCmd, sizeof(kStartupCmd));

  // TODO(yich): ExecuteCommand would mutate the command buffer, so we can't
  // mark the command buffer const here.
  tpm2::ExecuteCommand(sizeof(startup_cmd), startup_cmd, &response_size,
                       &response);
  LOG(INFO) << "TPM2_Startup(TPM_SU_CLEAR) sent.";

  ExtendPcr0BootMode(/*developer_mode=*/1, /*recovery_mode=*/0,
                     /*verified_firmware=*/0);
  // Assign an arbitrary value to PCR1.
  ExtendPcr(/*pcr_index=*/1, /*extend_data=*/"PCR1");
}

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

unsigned int GetCommandSize(const std::string& command) {
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

std::string RunCommand(const std::string& command) {
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
  CHECK_EQ(rc, TPM_RC_SUCCESS);
  rc = tpm2::UINT32_Unmarshal(&command_size, &header, &header_size);
  CHECK_EQ(rc, TPM_RC_SUCCESS);
  rc = tpm2::TPM_CC_Unmarshal(&command_code, &header, &header_size);
  if (command_code == TPM2_CC_SET_LOCALITY) {
    return CommandWithCode(TPM_RC_SUCCESS);
  }

  unsigned int response_size;
  unsigned char* response;
  tpm2::ExecuteCommand(command.size(), command_ptr, &response_size, &response);
  return std::string(reinterpret_cast<char*>(response), response_size);
}

}  // namespace

namespace tpm2_simulator {

int SimulatorDaemon::OnInit() {
  int exit_code = Daemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;
  InitializeVTPM();
  command_fd_ = RegisterVTPM();
  if (!command_fd_.is_valid()) {
    LOG(ERROR) << "Failed to register vTPM";
    return EX_OSERR;
  }
  command_fd_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      command_fd_.get(),
      base::BindRepeating(&SimulatorDaemon::OnCommand, base::Unretained(this)));
  return EX_OK;
}

void SimulatorDaemon::OnCommand() {
  char buffer[kMaxCommandSize];
  do {
    std::string request;
    remain_request_.swap(request);

    // Read request header.
    while (kHeaderSize > request.size()) {
      ssize_t size =
          HANDLE_EINTR(read(command_fd_.get(), buffer, kMaxCommandSize));
      CHECK_GE(size, 0);
      request.append(buffer, size);
    }

    const uint32_t command_size = GetCommandSize(request);

    // Read request body.
    while (command_size > request.size()) {
      ssize_t size =
          HANDLE_EINTR(read(command_fd_.get(), buffer, kMaxCommandSize));
      CHECK_GE(size, 0);
      request.append(buffer, size);
    }

    // Trim request.
    if (command_size < request.size()) {
      remain_request_ = request.substr(command_size);
      request.resize(command_size);
    }

    // Run command.
    std::string response = RunCommand(request);

    // Write response.
    if (!base::WriteFileDescriptor(command_fd_.get(), response.c_str(),
                                   response.size())) {
      PLOG(ERROR) << "WriteFileDescriptor failed.";
    }
  } while (!remain_request_.empty());
}

}  // namespace tpm2_simulator
