// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_helper.h"

#include <utility>

#include <base/containers/contains.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/switches/modemfwd_switches.h>

#include "modemfwd/logging.h"
#include "modemfwd/modem_sandbox.h"

namespace modemfwd {

namespace {

bool RunHelperProcessWithLogs(const HelperInfo& helper_info,
                              const std::vector<std::string>& arguments) {
  int child_stdout = -1, child_stderr = -1;
  std::vector<std::string> formatted_args;
  bool should_remove_capabilities = true;

  formatted_args.push_back(helper_info.executable_path.value());
  for (const std::string& argument : arguments)
    formatted_args.push_back("--" + argument);
  for (const std::string& extra_argument : helper_info.extra_arguments)
    formatted_args.push_back(extra_argument);

  // Determine where this helper's seccomp policy would be. Expected location:
  // /usr/share/policy/{modem_id}-helper-seccomp.policy
  const base::FilePath helper_seccomp_policy_file(base::StringPrintf(
      "%s/%s-seccomp.policy", kSeccompPolicyDirectory,
      helper_info.executable_path.BaseName().value().c_str()));

  // Allow cap_net_admin to persist if the helper requires it
  if (helper_info.net_admin_required)
    should_remove_capabilities = false;

  int exit_code = RunProcessInSandbox(
      formatted_args, helper_seccomp_policy_file, should_remove_capabilities,
      &child_stdout, &child_stderr);

  base::Time::Exploded time;
  base::Time::Now().LocalExplode(&time);
  const std::string output_log_file = base::StringPrintf(
      "%s/helper_log.%4u%02u%02u-%02u%02u%02u%03u", kModemfwdLogDirectory,
      time.year, time.month, time.day_of_month, time.hour, time.minute,
      time.second, time.millisecond);

  base::ScopedFD scoped_stdout(child_stdout);
  base::ScopedFD scoped_stderr(child_stderr);
  if (scoped_stdout.is_valid()) {
    base::File stdout_file = base::File(std::move(scoped_stdout));
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
  bool should_remove_capabilities = true;

  formatted_args.push_back(helper_info.executable_path.value());
  for (const std::string& argument : arguments)
    formatted_args.push_back("--" + argument);
  for (const std::string& extra_argument : helper_info.extra_arguments)
    formatted_args.push_back(extra_argument);

  // Determine where this helper's seccomp policy would be. Expected location:
  // /usr/share/policy/{modem_id}-helper-seccomp.policy
  const base::FilePath helper_seccomp_policy_file(base::StringPrintf(
      "%s/%s-seccomp.policy", kSeccompPolicyDirectory,
      helper_info.executable_path.BaseName().value().c_str()));

  // Allow cap_net_admin to persist if the helper requires it
  if (helper_info.net_admin_required)
    should_remove_capabilities = false;

  int exit_code = RunProcessInSandbox(
      formatted_args, helper_seccomp_policy_file, should_remove_capabilities,
      &child_stdout, &child_stderr);

  base::ScopedFD scoped_stdout(child_stdout);
  base::ScopedFD scoped_stderr(child_stderr);
  if (output && scoped_stdout.is_valid()) {
    base::File output_base_file = base::File(std::move(scoped_stdout));
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
class FlashMode {
 public:
  explicit FlashMode(const HelperInfo& helper_info)
      : helper_info_(helper_info) {}

  ~FlashMode() { RunHelperProcess(helper_info_, {kReboot}, nullptr); }

 private:
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

  bool GetFirmwareInfo(FirmwareInfo* out_info,
                       const std::string& firmware_revision) override {
    CHECK(out_info);
    std::string helper_output;
    if (!RunHelperProcess(helper_info_,
                          {kGetFirmwareInfo,
                           base::StringPrintf("%s=%s", kShillFirmwareRevision,
                                              firmware_revision.c_str())},
                          &helper_output)) {
      return false;
    }

    base::StringPairs parsed_versions;
    bool result = base::SplitStringIntoKeyValuePairs(helper_output, ':', '\n',
                                                     &parsed_versions);
    if (parsed_versions.size() == 0) {
      LOG(WARNING) << "Modem helper returned malformed firmware version info";
      return false;
    }

    if (!result) {
      LOG(WARNING) << "Modem helper returned malformed firmware version info,"
                   << " part of version info failed to parse.";
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
      else if (pair.first == "")
        continue;
      else
        out_info->assoc_versions.insert(pair);
    }

    return true;
  }

  // modemfwd::ModemHelper overrides.
  bool FlashFirmwares(const std::vector<FirmwareConfig>& configs) override {
    FlashMode flash_mode(helper_info_);

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

  std::optional<HeartbeatConfig> GetHeartbeatConfig() override {
    std::string output;
    if (!RunHelperProcess(helper_info_, {kGetHeartbeatConfig}, &output))
      return std::nullopt;

    base::StringPairs parsed_config;
    bool result =
        base::SplitStringIntoKeyValuePairs(output, ':', '\n', &parsed_config);
    if (!result) {
      LOG(WARNING) << "Modem helper returned malformed heartbeat config";
      return std::nullopt;
    }

    std::optional<int> max_failures;
    std::optional<int> interval_sec;
    std::optional<int> modem_idle_interval_sec;
    for (const auto& pair : parsed_config) {
      if (pair.first == kHeartbeatMaxFailures) {
        int value;
        if (!base::StringToInt(pair.second, &value))
          return std::nullopt;

        EVLOG(1) << __func__ << ": max_failures: " << value;
        max_failures = value;
      }
      if (pair.first == kHeartbeatInterval) {
        int value;
        if (!base::StringToInt(pair.second, &value))
          return std::nullopt;

        EVLOG(1) << __func__ << ": interval_sec: " << value;
        interval_sec = value;
      }
      if (pair.first == kHeartbeatModemIdleInterval) {
        int value;
        if (!base::StringToInt(pair.second, &value))
          return std::nullopt;

        EVLOG(1) << __func__ << ": modem_idle_interval: " << value;
        modem_idle_interval_sec = value;
      }
    }

    if (!max_failures.has_value() || !interval_sec.has_value()) {
      LOG(WARNING) << "Modem helper returned incomplete heartbeat config";
      return std::nullopt;
    }

    // |modem_idle_interval| is optional
    return HeartbeatConfig{
        .max_failures = *max_failures,
        .interval = base::Seconds(*interval_sec),
        .modem_idle_interval = modem_idle_interval_sec.has_value()
                                   ? base::Seconds(*modem_idle_interval_sec)
                                   : base::Seconds(0),
    };
  }

  std::vector<base::FilePath> GetRecoveryFileList(
      const base::FilePath& metadata_directory) override {
    std::string output;
    std::vector<base::FilePath> recovery_files;

    if (!RunHelperProcess(
            helper_info_,
            {kGetRecoveryFileList,
             base::StringPrintf("%s=%s", kRecoveryMetadataDir,
                                metadata_directory.value().c_str())},
            &output)) {
      return std::vector<base::FilePath>();
    }

    for (const auto& file : base::SplitString(
             output, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      recovery_files.emplace_back(file);
    }

    return recovery_files;
  }

 private:
  HelperInfo helper_info_;
};

std::unique_ptr<ModemHelper> CreateModemHelper(const HelperInfo& helper_info) {
  return std::make_unique<ModemHelperImpl>(helper_info);
}

}  // namespace modemfwd
