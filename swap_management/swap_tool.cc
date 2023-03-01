// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/swap_tool.h"
#include "swap_management/swap_tool_status.h"

#include <limits>

#include <base/files/dir_reader_posix.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/safe_strerror.h>
#include <base/strings/string_split.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/process/process.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>

namespace swap_management {

namespace {

constexpr char kSwapSizeFile[] = "/var/lib/swap/swap_size";
// This script holds the bulk of the real logic.
constexpr char kSwapHelperScriptFile[] = "/usr/share/cros/init/swap.sh";
constexpr char kZramDeviceFile[] = "/dev/zram0";
constexpr char kZramSysfsDir[] = "/sys/block/zram0";

constexpr base::TimeDelta kMaxIdleAge = base::Days(30);
constexpr uint64_t kMinFilelistDefaultValueKB = 1000000;

// TODO(b/218519699): Remove in deshell process.
std::string RunSwapHelper(std::vector<std::string> commands, int* result) {
  brillo::ProcessImpl process;

  process.AddArg(kSwapHelperScriptFile);
  for (auto& com : commands)
    process.AddArg(com);

  process.RedirectOutputToMemory(true);

  *result = process.Run();

  return process.GetOutputString(STDOUT_FILENO);
}

// TODO(b/218519699): Remove in deshell process.
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

// Helper function to run binary.
// On success, log stdout and return absl::OkStatus()
// On failure, return corresponding absl error based on errno and append stderr.
absl::Status SwapTool::RunProcessHelper(
    const std::vector<std::string>& commands) {
  if (commands.empty())
    return absl::InvalidArgumentError("Empty input for RunProcessHelper.");

  brillo::ProcessImpl process;
  for (auto& com : commands)
    process.AddArg(com);

  process.RedirectOutputToMemory(true);

  if (process.Run() != EXIT_SUCCESS)
    return ErrnoToStatus(errno, process.GetOutputString(STDOUT_FILENO));

  std::string out = process.GetOutputString(STDOUT_FILENO);
  if (!out.empty())
    LOG(INFO) << commands[0] << ": " << out;
  return absl::OkStatus();
}

absl::Status SwapTool::WriteFile(const base::FilePath& path,
                                 const std::string& data) {
  if (!base::WriteFile(path, data))
    return ErrnoToStatus(errno, "Failed to write " + path.value());

  return absl::OkStatus();
}

absl::Status SwapTool::ReadFileToStringWithMaxSize(const base::FilePath& path,
                                                   std::string* contents,
                                                   size_t max_size) {
  if (!base::ReadFileToStringWithMaxSize(path, contents, max_size))
    return ErrnoToStatus(errno, "Failed to read " + path.value());

  return absl::OkStatus();
}

absl::Status SwapTool::ReadFileToString(const base::FilePath& path,
                                        std::string* contents) {
  return ReadFileToStringWithMaxSize(path, contents,
                                     std::numeric_limits<size_t>::max());
}

absl::Status SwapTool::DeleteFile(const base::FilePath& path) {
  if (!base::DeleteFile(path))
    return ErrnoToStatus(errno, "Failed to delete " + path.value());

  return absl::OkStatus();
}

// Check if swap is already turned on.
absl::StatusOr<bool> SwapTool::IsZramSwapOn() {
  std::string swaps;
  absl::Status status = ReadFileToString(base::FilePath("/proc/swaps"), &swaps);
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

// Extract second field of MemTotal entry in /proc/meminfo. The unit for
// MemTotal is KiB.
absl::StatusOr<uint64_t> SwapTool::GetMemTotal() {
  std::string mem_info;
  absl::Status status =
      ReadFileToString(base::FilePath("/proc/meminfo"), &mem_info);
  if (!status.ok())
    return status;

  std::vector<std::string> mem_info_lines = base::SplitString(
      mem_info, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  for (auto& line : mem_info_lines) {
    if (line.find("MemTotal") != std::string::npos) {
      std::string buf = base::SplitString(line, " ", base::KEEP_WHITESPACE,
                                          base::SPLIT_WANT_NONEMPTY)[1];

      uint64_t res = 0;
      if (!absl::SimpleAtoi(buf, &res))
        return absl::OutOfRangeError("Failed to convert " + buf +
                                     " to 64-bit unsigned integer.");
      return res;
    }
  }

  return absl::NotFoundError("Could not get MemTotal in /proc/meminfo");
}

// Compute fraction of total RAM used for low-mem margin. The fraction is
// given in bips. A "bip" or "basis point" is 1/100 of 1%.
absl::Status SwapTool::SetDefaultLowMemoryMargin(uint64_t mem_total) {
  // Calculate critical margin in MiB, which is 5.2% free. Ignore the decimal.
  uint64_t critical_margin = (mem_total / 1024) * 0.052;
  // Calculate moderate margin in MiB, which is 40% free. Ignore the decimal.
  uint64_t moderate_margin = (mem_total / 1024) * 0.4;
  // Write into margin special file.
  return WriteFile(
      base::FilePath("/sys/kernel/mm/chromeos-low_mem/margin"),
      std::to_string(critical_margin) + " " + std::to_string(moderate_margin));
}

// Initialize MM tunnables.
absl::Status SwapTool::InitializeMMTunables(uint64_t mem_total) {
  absl::Status status = SetDefaultLowMemoryMargin(mem_total);
  if (!status.ok())
    return status;

  return WriteFile(base::FilePath("/proc/sys/vm/min_filelist_kbytes"),
                   std::to_string(kMinFilelistDefaultValueKB));
}

// Return zram (compressed ram disk) size in byte for swap.
// kSwapSizeFile contains the zram size in MiB.
// Empty or missing kSwapSizeFile means use default size, which is
// mem_total
// * 2.
// 0 means do not enable zram.
absl::StatusOr<uint64_t> SwapTool::GetZramSize(uint64_t mem_total) {
  // For security, only read first few bytes of kSwapSizeFile.
  std::string buf;
  absl::Status status =
      ReadFileToStringWithMaxSize(base::FilePath(kSwapSizeFile), &buf, 5);
  // If the file doesn't exist we use default zram size, other errors we must
  // propagate back.
  if (!status.ok() && !absl::IsNotFound(status))
    return status;

  // Trim the potential leading/trailing ASCII whitespaces.
  // Note that TrimWhitespaceASCII can safely use the same variable for inputs
  // and outputs.
  base::TrimWhitespaceASCII(buf, base::TRIM_ALL, &buf);

  if (absl::IsNotFound(status) || buf.empty())
    return mem_total * 1024 * 2;

  uint64_t requested_size_mib = 0;
  if (!absl::SimpleAtoi(buf, &requested_size_mib))
    return absl::OutOfRangeError("Failed to convert " +
                                 std::to_string(requested_size_mib) +
                                 " to 64-bit unsigned integer.");

  if (requested_size_mib == 0)
    return absl::InvalidArgumentError("Swap is not turned on since " +
                                      std::string(kSwapSizeFile) +
                                      " contains 0.");

  return requested_size_mib * 1024 * 1024;
}

// Run swapon to enable zram swapping.
// swapon may fail because of races with other programs that inspect all
// block devices, so try several times.
absl::Status SwapTool::EnableZramSwapping() {
  constexpr uint8_t kMaxEnableTries = 10;
  constexpr base::TimeDelta kRetryDelayUs = base::Milliseconds(100);
  absl::Status status = absl::OkStatus();

  for (size_t i = 0; i < kMaxEnableTries; i++) {
    status = RunProcessHelper({"/sbin/swapon", kZramDeviceFile});
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
    LOG(WARNING) << "swap is already on.";
    return absl::OkStatus();
  }

  absl::StatusOr<uint64_t> mem_total = GetMemTotal();
  if (!mem_total.ok())
    return mem_total.status();

  status = InitializeMMTunables(*mem_total);
  if (!status.ok())
    return status;

  absl::StatusOr<uint64_t> size_byte = GetZramSize(*mem_total);
  if (!size_byte.ok())
    return size_byte.status();

  // Load zram module. Ignore failure (it could be compiled in the kernel).
  if (!RunProcessHelper({"/sbin/modprobe", "zram"}).ok())
    LOG(WARNING) << "modprobe zram failed (compiled?)";

  // Set zram disksize.
  LOG(INFO) << "setting zram size to " << *size_byte << " bytes";
  status = WriteFile(base::FilePath("/sys/block/zram0/disksize"),
                     std::to_string(*size_byte));
  if (!status.ok())
    return status;

  // Set swap area.
  status = RunProcessHelper({"/sbin/mkswap", kZramDeviceFile});
  if (!status.ok())
    return status;

  return EnableZramSwapping();
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

  // It is possible that the Filename of swap file zram0 in /proc/swaps shows
  // wrong path "/zram0", since devtmpfs in minijail mount namespace is lazily
  // unmounted while swap_management terminates.
  // At this point we already know swap is on, with the only swap device
  // /dev/zram0 we have, anyway we turn off /dev/zram0, regardless what
  // /proc/swaps shows.
  absl::Status status =
      RunProcessHelper({"/sbin/swapoff", "-v", kZramDeviceFile});
  if (!status.ok())
    return status;

  // When we start up, we try to configure zram0, but it doesn't like to
  // be reconfigured on the fly.  Reset it so we can changes its params.
  // If there was a backing device being used, it will be automatically
  // removed because after it's created it was removed with deferred remove.
  return WriteFile(base::FilePath("/sys/block/zram0/reset"), "1");
}

// Set zram disksize in MiB.
// If `size` equals 0, set zram disksize to the default value.
absl::Status SwapTool::SwapSetSize(uint32_t size) {
  // Remove kSwapSizeFile so SwapStart will use default size for zram.
  if (size == 0)
    return DeleteFile(base::FilePath(kSwapSizeFile));

  if (size < 100 || size > 20000)
    return absl::InvalidArgumentError("Size is not between 100 and 20000 MiB.");

  return WriteFile(base::FilePath(kSwapSizeFile), std::to_string(size));
}

std::string SwapTool::SwapStatus() {
  std::stringstream output;
  std::string tmp;

  // Show general swap info first.
  if (ReadFileToString(base::FilePath("/proc/swaps"), &tmp).ok())
    output << tmp;

  // Show tunables.
  if (ReadFileToString(base::FilePath("/sys/kernel/mm/chromeos-low_mem/margin"),
                       &tmp)
          .ok())
    output << "low-memory margin (MiB): " + tmp;
  if (ReadFileToString(base::FilePath("/proc/sys/vm/min_filelist_kbytes"), &tmp)
          .ok())
    output << "min_filelist_kbytes (KiB): " + tmp;
  if (ReadFileToString(
          base::FilePath("/sys/kernel/mm/chromeos-low_mem/ram_vs_swap_weight"),
          &tmp)
          .ok())
    output << "ram_vs_swap_weight: " + tmp;
  if (ReadFileToString(base::FilePath("/proc/sys/vm/extra_free_kbytes"), &tmp)
          .ok())
    output << "extra_free_kbytes (KiB): " + tmp;

  // Show top entries in kZramSysfsDir for zram setting.
  base::DirReaderPosix dir_reader(kZramSysfsDir);
  if (dir_reader.IsValid()) {
    output << "\ntop-level entries in " + std::string(kZramSysfsDir) + ":\n";

    base::FilePath zram_sysfs(kZramSysfsDir);
    while (dir_reader.Next()) {
      std::string name = dir_reader.name();

      if (ReadFileToString(zram_sysfs.Append(name), &tmp).ok() &&
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
  base::FilePath enable_file =
      base::FilePath(kZramSysfsDir).Append("writeback_limit_enable");
  std::string msg;
  if (!WriteValueToFile(enable_file, "1", msg))
    return msg;

  base::FilePath filepath =
      base::FilePath(kZramSysfsDir).Append("writeback_limit");
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

  base::FilePath filepath = base::FilePath(kZramSysfsDir).Append("idle");
  std::string age_str = std::to_string(age.InSeconds());
  std::string msg;

  // We ignore the return value of WriteValueToFile because |msg|
  // contains the free form text response.
  WriteValueToFile(filepath, age_str, msg);
  return msg;
}

std::string SwapTool::InitiateSwapZramWriteback(uint32_t mode) const {
  base::FilePath filepath = base::FilePath(kZramSysfsDir).Append("writeback");
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
  size_t res = base::WriteFile(base::FilePath("/sys/kernel/mm/lru_gen/enabled"),
                               buf.c_str(), buf.size());
  if (res != buf.size()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         "org.chromium.SwapManagement.error.Swap",
                         strerror(errno));
    return false;
  }

  return true;
}

}  // namespace swap_management
