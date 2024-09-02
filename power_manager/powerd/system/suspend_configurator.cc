// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/suspend_configurator.h"

#include <vector>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/cpuinfo.h>
#include <brillo/file_utils.h>
#include <re2/re2.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"

namespace power_manager::system {

namespace {
// Path to write to configure system suspend mode.
static constexpr char kSuspendModePath[] = "/sys/power/mem_sleep";

// suspend to idle (S0iX) suspend mode
static constexpr char kSuspendModeFreeze[] = "s2idle";

// shallow/standby(S1) suspend mode
static constexpr char kSuspendModeShallow[] = "shallow";

// deep sleep(S3) suspend mode
static constexpr char kSuspendModeDeep[] = "deep";

// Pref value to use the kernel's default mode for suspend.
constexpr std::string_view kSuspendModeKernelDefaultPref = "kernel_default";

// Last resume result as reported by ChromeOS EC.
static constexpr char kECLastResumeResultPath[] =
    "/sys/kernel/debug/cros_ec/last_resume_result";

// Bit that is set in |kECLastResumeResultPath| when EC times out waiting for AP
// s0ix transition after suspend.  Please look at
// Documentation/ABI/testing/debugfs-cros-ec kernel documentation for more info.
static constexpr unsigned kECResumeResultHangBit = 1 << 31;

// path to the node that we can read/write to to program the RTC wakealarm
static constexpr char kWakealarmPath[] = "/sys/class/rtc/rtc0/wakealarm";

}  // namespace

// Static.
const base::FilePath SuspendConfigurator::kConsoleSuspendPath(
    "/sys/module/printk/parameters/console_suspend");

void SuspendConfigurator::Init(
    feature::PlatformFeaturesInterface* platform_features,
    PrefsInterface* prefs,
    const base::FilePath& run_dir) {
  DCHECK(prefs);
  platform_features_ = platform_features;
  prefs_ = prefs;
  initial_suspend_mode_path_ = run_dir.Append(kInitialSuspendModeFileName);
  ConfigureConsoleForSuspend();
  ReadSuspendMode();
}

bool SuspendConfigurator::IsValidSuspendMode(std::string_view mem_sleep) {
  if (mem_sleep != kSuspendModeDeep && mem_sleep != kSuspendModeFreeze &&
      mem_sleep != kSuspendModeShallow) {
    return false;
  }
  return true;
}

std::optional<std::string> SuspendConfigurator::ReadPowerMemSleepValue() {
  base::FilePath suspend_mode_path =
      GetPrefixedFilePath(base::FilePath(kSuspendModePath));
  std::string contents;
  std::string mem_sleep;

  if (!base::ReadFileToString(suspend_mode_path, &contents)) {
    LOG(WARNING) << "Unable to read " << kSuspendModePath;
    return std::nullopt;
  }
  // The contents is a space separated list of mem_sleep methods
  // with the selected value enclosed in [].
  // For example, the contents might be:
  // `[s2idle] deep shallow`
  // See [mem_sleep](https://www.kernel.org/doc/Documentation/power/states.txt)

  // Select the word surrounded by []:
  if (!RE2::PartialMatch(contents, R"(\[(\w+)\])", &mem_sleep)) {
    LOG(WARNING) << "Unable to parse " << kSuspendModePath
                 << " contents: " << contents;
    return std::nullopt;
  }
  return mem_sleep;
}

bool SuspendConfigurator::SaveInitialSuspendMode(
    const std::optional<std::string>& state) {
  // Write an empty file when the mode was not able to be read.
  if (!brillo::WriteStringToFile(initial_suspend_mode_path_,
                                 state.value_or(""))) {
    PLOG(ERROR) << "Failed to create " << initial_suspend_mode_path_;
    return false;
  }
  return true;
}

void SuspendConfigurator::ReadInitialSuspendMode() {
  std::string mem_sleep;

  kernel_default_sleep_mode_ = {};

  // Save the initial suspend mode if it doesn't exist.
  if (!base::PathExists(initial_suspend_mode_path_)) {
    std::optional<std::string> state = ReadPowerMemSleepValue();
    if (!SaveInitialSuspendMode(state) || state == std::nullopt)
      return;
    mem_sleep = state.value();
  } else if (!base::ReadFileToString(initial_suspend_mode_path_, &mem_sleep)) {
    LOG(WARNING) << "Unable to read initial system suspend mode";
    return;
  }
  base::TrimWhitespaceASCII(mem_sleep, base::TRIM_ALL, &mem_sleep);
  if (!IsValidSuspendMode(mem_sleep)) {
    LOG(WARNING) << "Invalid initial system suspend mode: " << mem_sleep;
    return;
  }
  LOG(INFO) << "Initial system mem_sleep mode: " << mem_sleep;
  kernel_default_sleep_mode_ = mem_sleep;
}

// TODO(crbug.com/941298) Move powerd_suspend script here eventually.
uint64_t SuspendConfigurator::PrepareForSuspend(
    const base::TimeDelta& suspend_duration) {
  base::FilePath suspend_mode_path = base::FilePath(kSuspendModePath);
  if (!base::PathExists(GetPrefixedFilePath(suspend_mode_path))) {
    LOG(INFO) << "File " << kSuspendModePath
              << " does not exist. Not configuring suspend mode";
  } else if (!base::WriteFile(GetPrefixedFilePath(suspend_mode_path),
                              suspend_mode_)) {
    PLOG(ERROR) << "Failed to write " << suspend_mode_ << " to "
                << kSuspendModePath;
  } else {
    LOG(INFO) << "Suspend mode configured to " << suspend_mode_;
  }

  // Do this at the end so that system spends close to |suspend_duration| in
  // suspend.
  if (suspend_duration == base::TimeDelta()) {
    return 0;
  }

  if (!base::WriteFile(base::FilePath(kWakealarmPath), "0")) {
    PLOG(ERROR) << "Couldn't reset wakealarm";
    return 0;
  }

  if (!base::WriteFile(base::FilePath(kWakealarmPath),
                       std::string("+" + base::NumberToString(
                                             suspend_duration.InSeconds())))) {
    PLOG(ERROR) << "Couldn't program wakealarm";
    return 0;
  }

  std::string wakealarm_str;
  if (!base::ReadFileToString(base::FilePath(kWakealarmPath), &wakealarm_str)) {
    PLOG(ERROR) << "Couldn't read wakealarm";
    return 0;
  }

  char* endptr;
  uint64_t wa_val = std::strtoull(wakealarm_str.c_str(), &endptr, 0);
  if (wa_val == 0) {
    // Also the wakealarm should never be zero if properly programmed.
    LOG(ERROR) << "Invalid wakealarm value: '" << wa_val << "'";
    return 0;
  }

  return wa_val;
}

bool SuspendConfigurator::UndoPrepareForSuspend() {
  base::FilePath resume_result_path =
      GetPrefixedFilePath(base::FilePath(kECLastResumeResultPath));
  unsigned resume_result = 0;
  if (base::PathExists(resume_result_path) &&
      util::ReadHexUint32File(resume_result_path, &resume_result) &&
      resume_result & kECResumeResultHangBit) {
    // EC woke the system due to SLP_S0 transition timeout.
    LOG(INFO) << "Suspend failure. EC woke the system due to a timeout when "
                 "watching for SLP_S0 transitions";
    return false;
  }
  return true;
}

void SuspendConfigurator::ConfigureConsoleForSuspend() {
  bool pref_val = true;
  bool enable_console = true;

  // Limit disabling console for S0iX to Intel CPUs (b/175428322).
  if (HasIntelCpu()) {
    if (IsSerialConsoleEnabled()) {
      // If S0iX is enabled, default to disabling console (b/63737106).
      if (prefs_->GetBool(kSuspendToIdlePref, &pref_val) && pref_val)
        enable_console = false;
    }
  }

  // Overwrite the default if the pref is set.
  if (prefs_->GetBool(kEnableConsoleDuringSuspendPref, &pref_val))
    enable_console = pref_val;

  const char* console_suspend_val = enable_console ? "N" : "Y";
  base::FilePath console_suspend_path =
      GetPrefixedFilePath(SuspendConfigurator::kConsoleSuspendPath);
  if (!base::WriteFile(console_suspend_path, console_suspend_val)) {
    PLOG(ERROR) << "Failed to write " << console_suspend_val << " to "
                << console_suspend_path.value();
  }
  LOG(INFO) << "Console during suspend is "
            << (enable_console ? "enabled" : "disabled");
}

void SuspendConfigurator::ReadSuspendMode() {
  bool pref_val = true;
  ReadInitialSuspendMode();

  // If s2idle is enabled, we write "freeze" to "/sys/power/state". Let us also
  // write "s2idle" to "/sys/power/mem_sleep" just to be safe.
  if (prefs_->GetBool(kSuspendToIdlePref, &pref_val) && pref_val) {
    suspend_mode_ = kSuspendModeFreeze;
  } else if (prefs_->GetString(kSuspendModePref, &suspend_mode_)) {
    if (suspend_mode_ == kSuspendModeKernelDefaultPref) {
      if (kernel_default_sleep_mode_.has_value()) {
        suspend_mode_ = kernel_default_sleep_mode_.value();
        LOG(INFO) << "Using kernel default suspend mode " << suspend_mode_;
      } else {
        suspend_mode_ = kSuspendModeDeep;
        LOG(WARNING) << "Unknown kernel default suspend mode, defaulting to "
                     << suspend_mode_;
      }
    }
    if (!IsValidSuspendMode(suspend_mode_)) {
      LOG(WARNING) << "Invalid suspend mode pref : " << suspend_mode_;
      suspend_mode_ = kSuspendModeDeep;
    }
  } else {
    suspend_mode_ = kSuspendModeDeep;
  }
}

base::FilePath SuspendConfigurator::GetPrefixedFilePath(
    const base::FilePath& file_path) const {
  if (prefix_path_for_testing_.empty())
    return file_path;
  DCHECK(file_path.IsAbsolute());
  return prefix_path_for_testing_.Append(file_path.value().substr(1));
}

bool SuspendConfigurator::IsSerialConsoleEnabled() {
  std::string contents;

  if (!base::ReadFileToString(base::FilePath("/proc/consoles"), &contents)) {
    return false;
  }

  std::vector<std::string> consoles = base::SplitString(
      contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  bool rc = false;

  for (const std::string& con : consoles) {
    if (base::StartsWith(con, "ttynull", base::CompareCase::SENSITIVE)) {
      continue;
    } else if (base::StartsWith(con, "tty", base::CompareCase::SENSITIVE)) {
      rc = true;
      break;
    }
  }

  return rc;
}

bool SuspendConfigurator::HasIntelCpu() {
  // Use a static bool so that it only gets evaluated once. This code assumes
  // that the CPU architecture doesn't change at runtime.
  static bool result = [this] {
    std::optional<brillo::CpuInfo> c = brillo::CpuInfo::Create(
        GetPrefixedFilePath(brillo::CpuInfo::DefaultPath()));
    if (!c.has_value()) {
      LOG(ERROR) << "Could not read cpuinfo";
      return false;
    }
    std::optional<std::string_view> res = c->LookUp(0, "vendor_id");
    return res.has_value() ? res.value() == "GenuineIntel" : false;
  }();
  return result;
}

}  // namespace power_manager::system
