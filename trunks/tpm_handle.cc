// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/tpm_handle.h"

#include <fcntl.h>
#include <unistd.h>
#include <utility>

#include <base/callback.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>

#include "trunks/tpm_generated.h"
#include "trunks/trunks_metrics.h"

namespace trunks {

namespace {

const char kTpmDevice[] = "/dev/tpm0";
const uint32_t kTpmBufferSize = 4096;
const int kInvalidFileDescriptor = -1;

// Retry parameters for opening /dev/tpm0.
// How long do we wait after the first try?
constexpr base::TimeDelta kInitialRetry = base::Seconds(0.1);
// When we retry the next time, how much longer do we wait?
constexpr double kRetryMultiplier = 2.0;
// How many times to retry?
constexpr int kMaxRetry = 5;
// Total of 4 wait time between 5 retries.
// sum 0.1*2^k for k = 0 to 3 = 1.5s
// Note that if this period is not enough, upstart will still respawn trunksd
// after it all fall through.

TPM_CC GetCommandCode(const std::string& command) {
  std::string buffer = command;
  TPM_ST tag;
  UINT32 command_size;
  TPM_CC command_code = 0;
  // Parse the header to get the command code
  TPM_RC rc = Parse_TPM_ST(&buffer, &tag, nullptr);
  DCHECK_EQ(rc, TPM_RC_SUCCESS);
  rc = Parse_UINT32(&buffer, &command_size, nullptr);
  DCHECK_EQ(rc, TPM_RC_SUCCESS);
  rc = Parse_TPM_CC(&buffer, &command_code, nullptr);
  DCHECK_EQ(rc, TPM_RC_SUCCESS);
  return command_code;
}

TPM_RC GetResponseCode(const std::string& response) {
  std::string buffer = response;
  TPM_ST tag;
  UINT32 response_size;
  TPM_RC response_code = 0;
  // Parse the header to get the command code
  TPM_RC rc = Parse_TPM_ST(&buffer, &tag, nullptr);
  DCHECK_EQ(rc, TPM_RC_SUCCESS);
  rc = Parse_UINT32(&buffer, &response_size, nullptr);
  DCHECK_EQ(rc, TPM_RC_SUCCESS);
  rc = Parse_TPM_RC(&buffer, &response_code, nullptr);
  DCHECK_EQ(rc, TPM_RC_SUCCESS);
  return response_code;
}

}  // namespace

TpmHandle::TpmHandle() : fd_(kInvalidFileDescriptor) {}

TpmHandle::~TpmHandle() {
  int result = IGNORE_EINTR(close(fd_));
  if (result == -1) {
    PLOG(ERROR) << "TPM: couldn't close " << kTpmDevice;
  }
  LOG(INFO) << "TPM: " << kTpmDevice << " closed successfully";
}

bool TpmHandle::Init() {
  if (fd_ != kInvalidFileDescriptor) {
    VLOG(1) << "Tpm already initialized.";
    return true;
  }
  base::TimeDelta current_wait = kInitialRetry;
  for (int i = 0; i < kMaxRetry; i++) {
    fd_ = HANDLE_EINTR(open(kTpmDevice, O_RDWR));
    if (fd_ == kInvalidFileDescriptor) {
      PLOG(ERROR) << "TPM: Error opening tpm0 file descriptor at "
                  << kTpmDevice;
      if (i == kMaxRetry - 1) {
        // If we get here, it doesn't work.
        return false;
      }
      base::PlatformThread::Sleep(current_wait);
      current_wait = current_wait * kRetryMultiplier;
      continue;
    }
    LOG(INFO) << "TPM: " << kTpmDevice << " opened successfully";
    break;
  }
  return true;
}

void TpmHandle::SendCommand(const std::string& command,
                            ResponseCallback callback) {
  std::move(callback).Run(SendCommandAndWait(command));
}

std::string TpmHandle::SendCommandAndWait(const std::string& command) {
  std::string response;
  TPM_RC result = SendCommandInternal(command, &response);
  if (result != TPM_RC_SUCCESS) {
    response = CreateErrorResponse(result);
    // Send the command code and system uptime of the first timeout command
    if (errno == ETIME) {
      static bool has_reported = false;
      if (!has_reported) {
        TrunksMetrics metrics;
        TPM_CC command_code = GetCommandCode(command);
        if (metrics.ReportTpmHandleTimeoutCommandAndTime(result, command_code))
          has_reported = true;
      }
    }
  }
  TPM_RC response_code = GetResponseCode(response);
  if (response_code != TPM_RC_SUCCESS) {
    TrunksMetrics metrics;
    metrics.ReportTpmErrorCode(response_code);
  }
  return response;
}

TPM_RC TpmHandle::SendCommandInternal(const std::string& command,
                                      std::string* response) {
  CHECK_NE(fd_, kInvalidFileDescriptor);
  int result = HANDLE_EINTR(write(fd_, command.data(), command.length()));
  if (result < 0 && errno == EREMOTEIO) {
    // Retry once in case the error is caused by late wakeup from sleep.
    // Repeated error should lead to failure.
    LOG(WARNING) << "TPM: Retrying write after Remote I/O error.";
    result = HANDLE_EINTR(write(fd_, command.data(), command.length()));
  }
  if (result < 0) {
    PLOG(ERROR) << "TPM: Error writing to TPM handle.";
    return TRUNKS_RC_WRITE_ERROR;
  }
  if (static_cast<size_t>(result) != command.length()) {
    LOG(ERROR) << "TPM: Error writing to TPM handle: " << result << " vs "
               << command.length();
    return TRUNKS_RC_WRITE_ERROR;
  }
  char response_buf[kTpmBufferSize];
  result = HANDLE_EINTR(read(fd_, response_buf, kTpmBufferSize));
  if (result < 0) {
    PLOG(ERROR) << "TPM: Error reading from TPM handle.";
    return TRUNKS_RC_READ_ERROR;
  }
  response->assign(response_buf, static_cast<size_t>(result));
  return TPM_RC_SUCCESS;
}

}  // namespace trunks
