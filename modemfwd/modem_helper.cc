// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_helper.h"

#include <linux/securebits.h>
#include <sys/capability.h>

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>
#include <chromeos/switches/modemfwd_switches.h>
#include <libminijail.h>
#include <scoped_minijail.h>

namespace modemfwd {

namespace {

// This lock file prevents powerd from suspending the system. Take it
// while we are attempting to flash the modem.
constexpr char kPowerOverrideLockFilePath[] =
    "/run/lock/power_override/modemfwd.lock";

constexpr char kModemfwdLogDirectory[] = "/var/log/modemfwd";
constexpr char kSeccompPolicyDirectory[] = "/usr/share/policy";

// For security reasons, we want to apply security restrictions to helpers:
// 1. We want to provide net admin capabilities only when necessary.
// 2. We want to apply helper-specific seccomp filter.
ScopedMinijail ConfigureSandbox(const HelperInfo& helper_info) {
  ScopedMinijail j(minijail_new());

  // Ensure no capability escalation occurs in the jail.
  minijail_no_new_privs(j.get());

  // Avoid setting securebits as we are running inside a minijail already.
  // See b/112030238 for justification.
  minijail_skip_setting_securebits(j.get(), SECURE_ALL_BITS | SECURE_ALL_LOCKS);

  // Remove all capabilities if helper doesn't require cap_net_admin by setting
  // sandboxed capabilities to 0. Only FM350 modem requires cap_net_admin.
  if (!base::Contains(helper_info.executable_path.value(), "fm350")) {
    LOG(INFO) << "Removing all capabilities from helper";
    minijail_use_caps(j.get(), 0);
  }

  // Determine where this helper's seccomp policy would be. Expected location:
  // /usr/share/policy/{modem_id}-helper-seccomp.policy
  const base::FilePath helper_seccomp_policy_file(base::StringPrintf(
      "%s/%s-seccomp.policy", kSeccompPolicyDirectory,
      helper_info.executable_path.BaseName().value().c_str()));

  // Apply seccomp filter, if it exists for this helper.
  if (base::PathExists(helper_seccomp_policy_file)) {
    LOG(INFO) << "Running helper with policy: "
              << helper_seccomp_policy_file.value();
    minijail_use_seccomp_filter(j.get());
    minijail_parse_seccomp_filters(j.get(),
                                   helper_seccomp_policy_file.value().c_str());
  } else {
    LOG(WARNING) << "No seccomp policy found for helper: "
                 << helper_info.executable_path.BaseName().value();
  }

  return j;
}

int RunProcessInSandbox(const HelperInfo& helper_info,
                        const std::vector<std::string>& formatted_args,
                        int* child_stdout,
                        int* child_stderr) {
  pid_t pid = -1;
  int child_stdin = -1;
  std::vector<char*> args;

  for (const std::string& argument : formatted_args)
    args.push_back(const_cast<char*>(argument.c_str()));

  args.push_back(nullptr);

  // Create sandbox and run helper.
  ScopedMinijail j = ConfigureSandbox(helper_info);
  int ret = minijail_run_pid_pipes(j.get(), args[0], args.data(), &pid,
                                   &child_stdin, child_stdout, child_stderr);

  if (ret != 0) {
    LOG(ERROR) << "Failed to run minijail: " << strerror(-ret);
    return ret;
  }

  return minijail_wait(j.get());
}

bool RunHelperProcessWithLogs(const HelperInfo& helper_info,
                              const std::vector<std::string>& arguments) {
  int child_stdout = -1, child_stderr = -1;
  std::vector<std::string> formatted_args;

  formatted_args.push_back(helper_info.executable_path.value());
  for (const std::string& argument : arguments)
    formatted_args.push_back("--" + argument);
  for (const std::string& extra_argument : helper_info.extra_arguments)
    formatted_args.push_back(extra_argument);

  int exit_code = RunProcessInSandbox(helper_info, formatted_args,
                                      &child_stdout, &child_stderr);

  base::Time::Exploded time;
  base::Time::Now().LocalExplode(&time);
  const std::string output_log_file = base::StringPrintf(
      "%s/helper_log.%4u%02u%02u-%02u%02u%02u%03u", kModemfwdLogDirectory,
      time.year, time.month, time.day_of_month, time.hour, time.minute,
      time.second, time.millisecond);

  if (child_stdout != -1) {
    base::File stdout_file = base::File(child_stdout);
    base::File dest_stdout_file =
        base::File(base::FilePath(output_log_file),
                   base::File::FLAG_CREATE | base::File::FLAG_WRITE);

    base::CopyFileContents(stdout_file, dest_stdout_file);
  }

  if (exit_code != 0) {
    LOG(ERROR) << "Failed to perform \"" << base::JoinString(arguments, " ")
               << "\" on the modem with retcode " << exit_code;
    return false;
  }

  return true;
}

bool RunHelperProcess(const HelperInfo& helper_info,
                      const std::vector<std::string>& arguments,
                      std::string* output) {
  int child_stdout = -1, child_stderr = -1;
  std::vector<std::string> formatted_args;

  formatted_args.push_back(helper_info.executable_path.value());
  for (const std::string& argument : arguments)
    formatted_args.push_back("--" + argument);
  for (const std::string& extra_argument : helper_info.extra_arguments)
    formatted_args.push_back(extra_argument);

  int exit_code = RunProcessInSandbox(helper_info, formatted_args,
                                      &child_stdout, &child_stderr);

  if (output && child_stdout != -1) {
    base::File output_base_file = base::File(child_stdout);
    DCHECK(output_base_file.IsValid());

    const int kBufSize = 1024;
    char buf[kBufSize];
    int bytes_read = output_base_file.ReadAtCurrentPos(buf, kBufSize);
    if (bytes_read != -1)
      output->assign(buf, bytes_read);
  }

  if (exit_code != 0) {
    LOG(ERROR) << "Failed to perform \"" << base::JoinString(arguments, " ")
               << "\" on the modem with retcode " << exit_code;
    return false;
  }

  return true;
}

// Ensures we reboot the modem to prevent us from leaving it in a bad state.
// Also takes a power override lock so we don't suspend while we're in the
// middle of flashing and ensures it's cleaned up later.
class FlashMode {
 public:
  static std::unique_ptr<FlashMode> Create(const HelperInfo& helper_info) {
    const base::FilePath lock_path(kPowerOverrideLockFilePath);
    // If the lock directory doesn't exist, then powerd is probably not running.
    // Don't worry about it in that case.
    if (base::DirectoryExists(lock_path.DirName())) {
      base::File lock_file(lock_path,
                           base::File::FLAG_CREATE | base::File::FLAG_WRITE);
      if (lock_file.IsValid()) {
        std::string lock_contents = base::StringPrintf("%d", getpid());
        lock_file.WriteAtCurrentPos(lock_contents.data(), lock_contents.size());
      }
    }

    if (!RunHelperProcess(helper_info, {kPrepareToFlash}, nullptr)) {
      base::DeleteFile(lock_path);
      return nullptr;
    }

    return base::WrapUnique(new FlashMode(helper_info));
  }

  ~FlashMode() {
    RunHelperProcess(helper_info_, {kReboot}, nullptr);
    base::DeleteFile(base::FilePath(kPowerOverrideLockFilePath));
  }

 private:
  // Use the static factory method above.
  explicit FlashMode(const HelperInfo& helper_info)
      : helper_info_(helper_info) {}
  FlashMode(const FlashMode&) = delete;
  FlashMode& operator=(const FlashMode&) = delete;

  HelperInfo helper_info_;
};

}  // namespace

class ModemHelperImpl : public ModemHelper {
 public:
  explicit ModemHelperImpl(const HelperInfo& helper_info)
      : helper_info_(helper_info) {}
  ModemHelperImpl(const ModemHelperImpl&) = delete;
  ModemHelperImpl& operator=(const ModemHelperImpl&) = delete;

  ~ModemHelperImpl() override = default;

  bool GetFirmwareInfo(FirmwareInfo* out_info) override {
    CHECK(out_info);

    std::string helper_output;
    if (!RunHelperProcess(helper_info_, {kGetFirmwareInfo}, &helper_output))
      return false;

    base::StringPairs parsed_versions;
    bool result = base::SplitStringIntoKeyValuePairs(helper_output, ':', '\n',
                                                     &parsed_versions);
    if (!result || parsed_versions.size() == 0) {
      LOG(WARNING) << "Modem helper returned malformed firmware version info";
      return false;
    }

    for (const auto& pair : parsed_versions) {
      if (pair.first == kFwMain)
        out_info->main_version = pair.second;
      else if (pair.first == kFwCarrier)
        out_info->carrier_version = pair.second;
      else if (pair.first == kFwCarrierUuid)
        out_info->carrier_uuid = pair.second;
      else if (pair.first == kFwOem)
        out_info->oem_version = pair.second;
      else
        out_info->assoc_versions.insert(pair);
    }

    return true;
  }

  // modemfwd::ModemHelper overrides.
  bool FlashFirmwares(const std::vector<FirmwareConfig>& configs) override {
    auto flash_mode = FlashMode::Create(helper_info_);
    if (!flash_mode)
      return false;

    if (!configs.size())
      return false;

    std::vector<std::string> firmwares;
    std::vector<std::string> versions;
    for (const auto& config : configs) {
      firmwares.push_back(base::StringPrintf("%s:%s", config.fw_type.c_str(),
                                             config.path.value().c_str()));
      versions.push_back(base::StringPrintf("%s:%s", config.fw_type.c_str(),
                                            config.version.c_str()));
    }

    return RunHelperProcessWithLogs(
        helper_info_,
        {base::StringPrintf("%s=%s", kFlashFirmware,
                            base::JoinString(firmwares, ",").c_str()),
         base::StringPrintf("%s=%s", kFwVersion,
                            base::JoinString(versions, ",").c_str())});
  }

  bool FlashModeCheck() override {
    std::string output;
    if (!RunHelperProcess(helper_info_, {kFlashModeCheck}, &output))
      return false;

    return base::TrimWhitespaceASCII(output, base::TRIM_ALL) == "true";
  }

  bool Reboot() override {
    return RunHelperProcess(helper_info_, {kReboot}, nullptr);
  }

  bool ClearAttachAPN(const std::string& carrier_uuid) override {
    return RunHelperProcess(
        helper_info_,
        {base::StringPrintf("%s=%s", kClearAttachAPN, carrier_uuid.c_str())},
        nullptr);
  }

 private:
  HelperInfo helper_info_;
};

std::unique_ptr<ModemHelper> CreateModemHelper(const HelperInfo& helper_info) {
  return std::make_unique<ModemHelperImpl>(helper_info);
}

}  // namespace modemfwd
