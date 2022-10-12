// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/swap_tool.h"

#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/safe_strerror.h>
#include <brillo/process/process.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>

namespace swap_management {

namespace {

// The path of the MGLRU enable file.
base::FilePath kMGLRUEnabledPath("/sys/kernel/mm/lru_gen/enabled");
// This script holds the bulk of the real logic.
base::FilePath kSwapHelperScript("/usr/share/cros/init/swap.sh");
base::FilePath kZramDevicePath("/sys/block/zram0");

constexpr base::TimeDelta kMaxIdleAge = base::Days(30);

std::string RunSwapHelper(std::vector<std::string> commands, int* result) {
  brillo::ProcessImpl process;

  process.AddArg(kSwapHelperScript.value().c_str());
  for (auto& com : commands)
    process.AddArg(com);

  process.RedirectOutputToMemory(true);

  *result = process.Run();

  return process.GetOutputString(STDOUT_FILENO);
}

bool WriteValueToFile(const base::FilePath& file,
                      const std::string& val,
                      std::string& msg) {
  if (!base::WriteFile(file, val)) {
    msg = base::StringPrintf("ERROR: Failed to write %s to %s. Error %d (%s)",
                             val.c_str(), file.MaybeAsASCII().c_str(), errno,
                             base::safe_strerror(errno).c_str());
    return false;
  }

  msg = "SUCCESS";

  return true;
}
}  // namespace

std::string SwapTool::SwapEnable(int32_t size, bool change_now) const {
  int result;
  std::string output;

  output = RunSwapHelper({"enable", std::to_string(size)}, &result);

  if (result != EXIT_SUCCESS)
    return output;

  if (change_now)
    output = SwapStartStop(true);

  return output;
}

std::string SwapTool::SwapDisable(bool change_now) const {
  int result;
  std::string output;

  output = RunSwapHelper({"disable"}, &result);

  if (result != EXIT_SUCCESS)
    return output;

  if (change_now)
    output = SwapStartStop(true);

  return output;
}

std::string SwapTool::SwapStartStop(bool on) const {
  int result;
  std::string output;

  output = RunSwapHelper({"stop"}, &result);

  if (result != EXIT_SUCCESS)
    return output;

  if (on)
    output = RunSwapHelper({"start"}, &result);

  return output;
}

std::string SwapTool::SwapStatus() const {
  int result;
  return RunSwapHelper({"status"}, &result);
}

std::string SwapTool::SwapSetParameter(const std::string& parameter_name,
                                       uint32_t parameter_value) const {
  int result;

  return RunSwapHelper(
      {"set_parameter", parameter_name, std::to_string(parameter_value)},
      &result);
}

// Zram writeback configuration.
std::string SwapTool::SwapZramEnableWriteback(uint32_t size_mb) const {
  int result;

  // For now throw out values > 32gb.
  constexpr int kMaxSizeMb = 32 * 1024;
  if (size_mb == 0 || size_mb >= kMaxSizeMb)
    return "ERROR: Invalid size specified.";

  std::string res = RunSwapHelper(
      {"enable_zram_writeback", std::to_string(size_mb)}, &result);
  if (result && res.empty())
    res = "unknown error";

  return std::string(result ? "ERROR: " : "SUCCESS: ") + res;
}

std::string SwapTool::SwapZramSetWritebackLimit(uint32_t num_pages) const {
  base::FilePath enable_file(kZramDevicePath.Append("writeback_limit_enable"));
  std::string msg;
  if (!WriteValueToFile(enable_file, "1", msg))
    return msg;

  base::FilePath filepath(kZramDevicePath.Append("writeback_limit"));
  std::string pages_str = std::to_string(num_pages);

  // We ignore the return value of WriteValueToFile because |msg|
  // contains the free form text response.
  WriteValueToFile(filepath, pages_str, msg);
  return msg;
}

std::string SwapTool::SwapZramMarkIdle(uint32_t age_seconds) const {
  const auto age = base::Seconds(age_seconds);
  if (age > kMaxIdleAge) {
    // Only allow marking pages as idle between 0sec and 30 days.
    return base::StringPrintf("ERROR: Invalid age: %d", age_seconds);
  }

  base::FilePath filepath(kZramDevicePath.Append("idle"));
  std::string age_str = std::to_string(age.InSeconds());
  std::string msg;

  // We ignore the return value of WriteValueToFile because |msg|
  // contains the free form text response.
  WriteValueToFile(filepath, age_str, msg);
  return msg;
}

std::string SwapTool::InitiateSwapZramWriteback(uint32_t mode) const {
  base::FilePath filepath(kZramDevicePath.Append("writeback"));
  std::string mode_str;
  if (mode == WRITEBACK_IDLE) {
    mode_str = "idle";
  } else if (mode == WRITEBACK_HUGE) {
    mode_str = "huge";
  } else if (mode == WRITEBACK_HUGE_IDLE) {
    mode_str = "huge_idle";
  } else {
    return "ERROR: Invalid mode";
  }

  std::string msg;

  // We ignore the return value of WriteValueToFile because |msg|
  // contains the free form text response.
  WriteValueToFile(filepath, mode_str, msg);
  return msg;
}

bool SwapTool::MGLRUSetEnable(brillo::ErrorPtr* error, bool enable) const {
  std::string buf = std::to_string(enable ? 1 : 0);

  errno = 0;
  size_t res = base::WriteFile(base::FilePath(kMGLRUEnabledPath), buf.c_str(),
                               buf.size());
  if (res != buf.size()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.Swap",
                         strerror(errno));
    return false;
  }

  return true;
}

}  // namespace swap_management
