// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "featured/c_feature_library.h"
#include "swap_management/swap_tool.h"
#include "swap_management/utils.h"
#include "swap_management/zram_idle.h"
#include "swap_management/zram_writeback.h"

#include <absl/status/status.h>
#include <base/files/dir_reader_posix.h>
#include <base/logging.h>
#include <base/posix/safe_strerror.h>
#include <base/process/process_metrics.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

namespace swap_management {

namespace {

constexpr char kSwapSizeFile[] = "/var/lib/swap/swap_size";
constexpr char kSwapRecompAlgorithmFile[] =
    "/var/lib/swap/swap_recomp_algorithm";
// The default size of zram is twice the device's memory size.
constexpr float kDefaultZramSizeToMemTotalMultiplier = 2.0;

constexpr char kSwapZramCompAlgorithmFeatureName[] =
    "CrOSLateBootSwapZramCompAlgorithm";
constexpr VariationsFeature kSwapZramCompAlgorithmFeature{
    kSwapZramCompAlgorithmFeatureName, FEATURE_DISABLED_BY_DEFAULT};
constexpr char kSwapZramDisksizeFeatureName[] = "CrOSLateBootSwapZramDisksize";
constexpr VariationsFeature kSwapZramDisksizeFeature{
    kSwapZramDisksizeFeatureName, FEATURE_DISABLED_BY_DEFAULT};
constexpr char kSwapZramWritebackFeatureName[] =
    "CrOSLateBootSwapZramWriteback";
constexpr VariationsFeature kSwapZramWritebackFeature{
    kSwapZramWritebackFeatureName, FEATURE_DISABLED_BY_DEFAULT};
}  // namespace

SwapTool::SwapTool(feature::PlatformFeatures* platform_features)
    : platform_features_(platform_features) {}

// Check if swap is already turned on.
absl::StatusOr<bool> SwapTool::IsZramSwapOn() {
  std::string swaps;
  absl::Status status =
      Utils::Get()->ReadFileToString(base::FilePath("/proc/swaps"), &swaps);
  if (!status.ok())
    return status;

  std::vector<std::string> swaps_lines = base::SplitString(
      swaps, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  // Skip the first line which is header. Swap is turned on if swaps_lines
  // contains entry with zram0 keyword.
  for (size_t i = 1; i < swaps_lines.size(); i++) {
    if (swaps_lines[i].find("zram0") != std::string::npos)
      return true;
  }

  return false;
}

// Return user runtime config zram size in byte for swap.
// kSwapSizeFile contains the zram size in MiB.
// Return 0 if swap is disabled, and NotFoundError if kSwapSizeFile is empty.
// Otherwise propagate error back, and the following code should calculate zram
// size based on MemTotal/features instead.
absl::StatusOr<uint64_t> SwapTool::GetUserConfigZramSizeBytes() {
  // For security, only read first few bytes of kSwapSizeFile.
  std::string buf;
  absl::Status status = Utils::Get()->ReadFileToStringWithMaxSize(
      base::FilePath(kSwapSizeFile), &buf, 5);
  if (!status.ok())
    return status;

  // Trim the potential leading/trailing ASCII whitespaces.
  // Note that TrimWhitespaceASCII can safely use the same variable for inputs
  // and outputs.
  base::TrimWhitespaceASCII(buf, base::TRIM_ALL, &buf);
  if (buf.empty())
    return absl::InvalidArgumentError(std::string(kSwapSizeFile) +
                                      " is empty.");

  absl::StatusOr<uint64_t> requested_size_mib =
      Utils::Get()->SimpleAtoi<uint64_t>(buf);
  if (!requested_size_mib.ok())
    return requested_size_mib.status();

  if (*requested_size_mib == 0)
    LOG(WARNING) << "swap is disabled since " << std::string(kSwapSizeFile)
                 << " contains 0.";

  return (*requested_size_mib) * 1024 * 1024;
}

// Set comp_algorithm if kSwapZramCompAlgorithmFeature is enabled.
void SwapTool::SetCompAlgorithmIfOverriden() {
  std::optional<std::string> comp_algorithm =
      GetFeatureParam(kSwapZramCompAlgorithmFeature, "comp_algorithm");
  if (comp_algorithm.has_value()) {
    LOG(INFO) << "Setting zram comp_algorithm to " << *comp_algorithm;
    absl::Status status = Utils::Get()->WriteFile(
        base::FilePath(kZramSysfsDir).Append("comp_algorithm"),
        *comp_algorithm);
    LOG_IF(WARNING, !status.ok())
        << "Failed to set zram comp_algorithm: " << status;
  }
}
// Get zram size in byte.
// There are two factor to decide the size: user runtime config and
// feature.
// 1. User runtime config:
//    Read size in MiB in kSwapSizeFile (programmed by SwapSetSize).
//    0 means disable zram.
// 2. Feature (kSwapZramDisksizeFeature):
//    If the feature is available, load multiplier from features.
//    Then size = mem_total * multiplier (2 by default).
// We first check if user runtime config is available, if not then
// feature, if not then finally using default zram size.
absl::StatusOr<uint64_t> SwapTool::GetZramSizeBytes() {
  // 1. User runtime config
  absl::StatusOr<uint64_t> size_byte = GetUserConfigZramSizeBytes();
  // Return since user has runtime config for zram size, or swap is disabled.
  if (size_byte.ok())
    return size_byte;
  // Let's provide log for errors other than NotFoundError which is valid, and
  // continue.
  LOG_IF(WARNING, !absl::IsNotFound(size_byte.status()))
      << "Failed to get user config zram size: " << size_byte.status();

  // 2. Feature
  // First, read /proc/meminfo for MemTotal in kiB.
  absl::StatusOr<base::SystemMemoryInfoKB> meminfo =
      Utils::Get()->GetSystemMemoryInfo();
  if (!meminfo.ok())
    return meminfo.status();

  // Then check if feature kSwapZramDisksizeFeature is available.
  float multiplier = kDefaultZramSizeToMemTotalMultiplier;
  std::optional<std::string> feature_multiplier =
      GetFeatureParam(kSwapZramDisksizeFeature, "multiplier");
  if (feature_multiplier.has_value()) {
    if (!absl::SimpleAtof(*feature_multiplier, &multiplier)) {
      LOG(WARNING) << absl::OutOfRangeError(
          "Failed to convert " + *feature_multiplier +
          " to float. Using default zram size multiplier.");
      multiplier = kDefaultZramSizeToMemTotalMultiplier;
    }
  }

  // Should roundup with page size.
  return Utils::Get()->RoundupMultiple(
      static_cast<uint64_t>((*meminfo).total) * 1024 * multiplier, kPageSize);
}

// Program /sys/block/zram0/recomp_algorithm.
// For the format of |kSwapRecompAlgorithmFile|, please refer to the
// description in SwapZramSetRecompAlgorithms.
void SwapTool::SetRecompAlgorithms() {
  std::string buf;
  absl::Status status = Utils::Get()->ReadFileToString(
      base::FilePath(kSwapRecompAlgorithmFile), &buf);
  std::vector<std::string> algos = base::SplitString(
      buf, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (uint8_t i = 0; i < algos.size(); i++) {
    absl::Status status = Utils::Get()->WriteFile(
        base::FilePath(kZramSysfsDir).Append("recomp_algorithm"),
        "algo=" + algos[i] + " priority=" + std::to_string(i + 1));
    LOG_IF(WARNING, !status.ok())
        << "Failed to set zram recomp_algorithm: " << status;
  }
}

// Return value for params in feature if feature is enabled.
std::optional<std::string> SwapTool::GetFeatureParam(
    const VariationsFeature& vf, const std::string& key) {
  if (platform_features_) {
    feature::PlatformFeaturesInterface::ParamsResult result =
        platform_features_->GetParamsAndEnabledBlocking({&vf});
    if (result.find(vf.name) != result.end()) {
      // If not enabled.
      if (!result[vf.name].enabled)
        return std::nullopt;

      auto params = result[vf.name].params;
      if (params.find(key) != params.end())
        return params[key];
    }
  }

  return std::nullopt;
}

// Run swapon to enable zram swapping.
// swapon may fail because of races with other programs that inspect all
// block devices, so try several times.
absl::Status SwapTool::EnableZramSwapping() {
  constexpr uint8_t kMaxEnableTries = 10;
  constexpr base::TimeDelta kRetryDelayUs = base::Milliseconds(100);
  absl::Status status = absl::OkStatus();

  for (size_t i = 0; i < kMaxEnableTries; i++) {
    status = Utils::Get()->RunProcessHelper({"/sbin/swapon", kZramDeviceFile});
    if (status.ok())
      return status;

    LOG(WARNING) << "swapon " << kZramDeviceFile << " failed, try " << i
                 << " times, last error:" << status;

    base::PlatformThread::Sleep(kRetryDelayUs);
  }

  return absl::AbortedError("swapon " + std::string(kZramDeviceFile) +
                            " failed after " + std::to_string(kMaxEnableTries) +
                            " tries" + " last error: " + status.ToString());
}

absl::Status SwapTool::SwapStart() {
  absl::Status status = absl::OkStatus();

  // Return true if swap is already on.
  absl::StatusOr<bool> on = IsZramSwapOn();
  if (!on.ok())
    return on.status();
  if (*on) {
    LOG(WARNING) << "Swap is already on.";
    return absl::OkStatus();
  }

  // Get zram size.
  absl::StatusOr<uint64_t> size_byte = GetZramSizeBytes();
  if (!size_byte.ok() || *size_byte == 0)
    return status;

  // Load zram module. Ignore failure (it could be compiled in the kernel).
  if (!Utils::Get()->RunProcessHelper({"/sbin/modprobe", "zram"}).ok())
    LOG(WARNING) << "modprobe zram failed (compiled?)";

  // Set zram recompress algorithm if user has config.
  SetRecompAlgorithms();

  // Set zram compress algorithm if feature is available.
  SetCompAlgorithmIfOverriden();

  // Set zram size.
  LOG(INFO) << "Setting zram disksize to " << *size_byte << " bytes";
  status =
      Utils::Get()->WriteFile(base::FilePath(kZramSysfsDir).Append("disksize"),
                              std::to_string(*size_byte));
  if (!status.ok())
    return status;

  // Set swap area.
  status = Utils::Get()->RunProcessHelper({"/sbin/mkswap", kZramDeviceFile});
  if (!status.ok())
    return status;

  // Enable zram swap.
  status = EnableZramSwapping();
  if (!status.ok())
    return status;

  // Enable zram writeback if feature is available.
  status = EnableZramWriteback();
  LOG_IF(ERROR, !status.ok()) << "Failed to enable zram writeback: " << status;

  return status;
}

absl::Status SwapTool::SwapStop() {
  // Return false if swap is already off.
  absl::StatusOr<bool> on = IsZramSwapOn();
  if (!on.ok())
    return on.status();
  if (!*on) {
    LOG(WARNING) << "Swap is already off.";
    return absl::OkStatus();
  }

  // Stop zram writeback.
  ZramWriteback::Get()->Stop();

  // It is possible that the Filename of swap file zram0 in /proc/swaps shows
  // wrong path "/zram0", since devtmpfs in minijail mount namespace is lazily
  // unmounted while swap_management terminates.
  // At this point we already know swap is on, with the only swap device
  // /dev/zram0 we have, anyway we turn off /dev/zram0, regardless what
  // /proc/swaps shows.
  absl::Status status =
      Utils::Get()->RunProcessHelper({"/sbin/swapoff", "-v", kZramDeviceFile});
  if (!status.ok())
    return status;

  // When we start up, we try to configure zram0, but it doesn't like to
  // be reconfigured on the fly.  Reset it so we can changes its params.
  // If there was a backing device being used, it will be automatically
  // removed because after it's created it was removed with deferred remove.
  return Utils::Get()->WriteFile(base::FilePath(kZramSysfsDir).Append("reset"),
                                 "1");
}

// Set zram disksize in MiB.
// If `size` equals 0, set zram size file to the default value.
// If `size` is negative, set zram size file to 0. Swap is disabled if zram size
// file contains 0.
absl::Status SwapTool::SwapSetSize(int32_t size) {
  // Remove kSwapSizeFile so SwapStart will use default size for zram.
  if (size == 0) {
    return Utils::Get()->DeleteFile(base::FilePath(kSwapSizeFile));
  } else if (size < 0) {
    size = 0;
  } else if (size < 128 || size > 65000) {
    return absl::InvalidArgumentError("Size is not between 128 and 65000 MiB.");
  }

  return Utils::Get()->WriteFile(base::FilePath(kSwapSizeFile),
                                 std::to_string(size));
}

absl::Status SwapTool::SwapSetSwappiness(uint32_t swappiness) {
  // Only allow swappiness between 0 and 100.
  if (swappiness > 100)
    return absl::OutOfRangeError("Invalid swappiness " +
                                 std::to_string(swappiness));

  return Utils::Get()->WriteFile(base::FilePath("/proc/sys/vm/swappiness"),
                                 std::to_string(swappiness));
}

std::string SwapTool::SwapStatus() {
  std::stringstream output;
  std::string tmp;

  // Show general swap info first.
  if (Utils::Get()->ReadFileToString(base::FilePath("/proc/swaps"), &tmp).ok())
    output << tmp;

  // Show tunables.
  if (Utils::Get()
          ->ReadFileToString(base::FilePath("/proc/sys/vm/min_filelist_kbytes"),
                             &tmp)
          .ok())
    output << "min_filelist_kbytes (KiB): " + tmp;
  if (Utils::Get()
          ->ReadFileToString(base::FilePath("/proc/sys/vm/extra_free_kbytes"),
                             &tmp)
          .ok())
    output << "extra_free_kbytes (KiB): " + tmp;

  // Show top entries in kZramSysfsDir for zram setting.
  base::DirReaderPosix dir_reader(kZramSysfsDir);
  if (dir_reader.IsValid()) {
    output << "\ntop-level entries in " + std::string(kZramSysfsDir) + ":\n";

    base::FilePath zram_sysfs(kZramSysfsDir);
    while (dir_reader.Next()) {
      std::string name = dir_reader.name();

      if (Utils::Get()->ReadFileToString(zram_sysfs.Append(name), &tmp).ok() &&
          !tmp.empty()) {
        std::vector<std::string> lines = base::SplitString(
            tmp, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
        for (auto& line : lines)
          output << name + ": " + line + "\n";
      }
    }
  }

  return output.str();
}

absl::Status SwapTool::SwapZramEnableWriteback(uint32_t size_mb) {
  return ZramWriteback::Get()->EnableWriteback(size_mb);
}
absl::Status SwapTool::SwapZramSetWritebackLimit(uint32_t num_pages) {
  return ZramWriteback::Get()->SetWritebackLimit(num_pages);
}
absl::Status SwapTool::SwapZramMarkIdle(uint32_t age_seconds) {
  return MarkIdle(age_seconds);
}
absl::Status SwapTool::InitiateSwapZramWriteback(ZramWritebackMode mode) {
  return ZramWriteback::Get()->InitiateWriteback(mode);
}

absl::Status SwapTool::MGLRUSetEnable(uint8_t value) {
  return Utils::Get()->WriteFile(
      base::FilePath("/sys/kernel/mm/lru_gen/enabled"), std::to_string(value));
}

absl::Status SwapTool::InitiateSwapZramRecompression(ZramRecompressionMode mode,
                                                     uint32_t threshold,
                                                     const std::string& algo) {
  base::FilePath filepath = base::FilePath(kZramSysfsDir).Append("recompress");
  std::stringstream ss;
  if (mode == RECOMPRESSION_IDLE) {
    ss << "type=idle";
  } else if (mode == RECOMPRESSION_HUGE) {
    ss << "type=huge";
  } else if (mode == RECOMPRESSION_HUGE_IDLE) {
    ss << "type=huge_idle";
  } else if (mode != 0) {
    // |mode| can be optional.
    return absl::InvalidArgumentError("Invalid mode");
  }

  if (threshold != 0)
    ss << " threshold=" << std::to_string(threshold);

  // This specified algorithm has to be registered through
  // SwapZramSetRecompAlgorithms first.
  if (!algo.empty())
    ss << " algo=" << algo;

  return Utils::Get()->WriteFile(filepath, ss.str());
}

absl::Status SwapTool::SwapZramSetRecompAlgorithms(
    const std::vector<std::string>& algos) {
  // We store |algos| in |kSwapRecompAlgorithmFile| in priority order, using
  // space as delimiter: algo1 algo2 ... The next time SwapStart is executed,
  // /sys/block/zram0/recomp_algorithm will be programmed with algo1 with
  // priority 1, and algo2 with priority 2, etc.
  absl::Status status = absl::OkStatus();

  // With empty |algos|, we disable zram recompression by removing
  // |kSwapRecompAlgorithmFile|
  if (algos.empty())
    return Utils::Get()->DeleteFile(base::FilePath(kSwapRecompAlgorithmFile));

  const std::string joined = base::JoinString(algos, " ");
  return Utils::Get()->WriteFile(base::FilePath(kSwapRecompAlgorithmFile),
                                 joined);
}

absl::Status SwapTool::EnableZramWriteback() {
  // Check if feature (kSwapZramWritebackFeature) is enabled.
  if (!platform_features_) {
    LOG(WARNING) << "PlatformFeatures is not initialized.";
    return absl::OkStatus();
  }
  feature::PlatformFeaturesInterface::ParamsResult result =
      platform_features_->GetParamsAndEnabledBlocking(
          {&kSwapZramWritebackFeature});
  if (result.find(kSwapZramWritebackFeatureName) == result.end() ||
      !result[kSwapZramWritebackFeatureName].enabled) {
    LOG(INFO) << "CrOSLateBootSwapZramWriteback feature is not enabled.";
    return absl::OkStatus();
  }

  // Read config from feature and override the default.
  for (const auto& [key, value] :
       result[kSwapZramWritebackFeatureName].params) {
    absl::Status status =
        ZramWriteback::Get()->SetZramWritebackConfigIfOverriden(key, value);
    LOG_IF(WARNING, !status.ok()) << "Failed to set zram writeback config ["
                                  << key << ": " << value << "]: " << status;
  }

  return ZramWriteback::Get()->Start();
}

}  // namespace swap_management
