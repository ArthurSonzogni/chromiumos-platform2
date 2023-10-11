// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/swap_tool.h"
#include "featured/c_feature_library.h"
#include "swap_management/swap_tool_util.h"

#include <cinttypes>
#include <utility>

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

namespace swap_management {

namespace {

constexpr char kSwapSizeFile[] = "/var/lib/swap/swap_size";
constexpr char kSwapRecompAlgorithmFile[] =
    "/var/lib/swap/swap_recomp_algorithm";
constexpr char kZramDeviceFile[] = "/dev/zram0";
constexpr char kZramSysfsDir[] = "/sys/block/zram0";
constexpr char kZramWritebackName[] = "zram-writeback";
constexpr char kZramIntegrityName[] = "zram-integrity";
constexpr char kZramWritebackIntegrityMount[] = "/run/zram-integrity";
constexpr char kZramBackingDevice[] = "/sys/block/zram0/backing_dev";
constexpr char kStatefulPartitionDir[] =
    "/mnt/stateful_partition/unencrypted/userspace_swap.tmp";
constexpr uint32_t kMiB = 1048576;
constexpr uint32_t kSectorSize = 512;
// The default size of zram is twice the device's memory size.
constexpr float kDefaultZramSizeToMemTotalMultiplier = 2.0;

constexpr base::TimeDelta kMaxIdleAge = base::Days(30);

constexpr char kSwapZramCompAlgorithmFeatureName[] =
    "CrOSLateBootSwapZramCompAlgorithm";
constexpr VariationsFeature kSwapZramCompAlgorithmFeature{
    kSwapZramCompAlgorithmFeatureName, FEATURE_DISABLED_BY_DEFAULT};
constexpr char kSwapZramDisksizeFeatureName[] = "CrOSLateBootSwapZramDisksize";
constexpr VariationsFeature kSwapZramDisksizeFeature{
    kSwapZramDisksizeFeatureName, FEATURE_DISABLED_BY_DEFAULT};

// Round up multiple will round the first argument |number| up to the next
// multiple of the second argument |alignment|.
uint64_t RoundupMultiple(uint64_t number, uint64_t alignment) {
  return ((number + (alignment - 1)) / alignment) * alignment;
}

}  // namespace

absl::StatusOr<std::unique_ptr<LoopDev>> LoopDev::Create(
    const std::string& path) {
  return Create(path, false, 0);
}

absl::StatusOr<std::unique_ptr<LoopDev>> LoopDev::Create(
    const std::string& path, bool direct_io, uint32_t sector_size) {
  std::vector<std::string> command({"/sbin/losetup", "--show"});
  if (direct_io)
    command.push_back("--direct-io=on");
  if (sector_size != 0)
    command.push_back("--sector-size=" + std::to_string(sector_size));
  command.push_back("-f");
  command.push_back(path);

  std::string loop_dev_path;
  absl::Status status =
      SwapToolUtil::Get()->RunProcessHelper(command, &loop_dev_path);
  if (!status.ok())
    return status;
  base::TrimWhitespaceASCII(loop_dev_path, base::TRIM_ALL, &loop_dev_path);

  return std::unique_ptr<LoopDev>(new LoopDev(loop_dev_path));
}

LoopDev::~LoopDev() {
  absl::Status status = absl::OkStatus();

  if (!path_.empty()) {
    status =
        SwapToolUtil::Get()->RunProcessHelper({"/sbin/losetup", "-d", path_});
    LOG_IF(ERROR, !status.ok()) << status;
    path_.clear();
  }
}

std::string LoopDev::GetPath() {
  return path_;
}

absl::StatusOr<std::unique_ptr<DmDev>> DmDev::Create(
    const std::string& name, const std::string& table_fmt) {
  absl::Status status = absl::OkStatus();

  status = SwapToolUtil::Get()->RunProcessHelper(
      {"/sbin/dmsetup", "create", name, "--table", table_fmt});
  if (!status.ok())
    return status;

  std::unique_ptr<DmDev> dm_dev = std::unique_ptr<DmDev>(new DmDev(name));

  status = dm_dev->Wait();
  if (!status.ok())
    return status;

  return std::move(dm_dev);
}

DmDev::~DmDev() {
  absl::Status status = absl::OkStatus();

  if (!name_.empty()) {
    status = SwapToolUtil::Get()->RunProcessHelper(
        {"/sbin/dmsetup", "remove", "--deferred", name_});
    LOG_IF(ERROR, !status.ok()) << status;
    name_.clear();
  }
}

// Wait for up to 5 seconds for a dm device to become available,
// if it doesn't then return failed status. This is needed because dm devices
// may take a few seconds to become visible at /dev/mapper after the table is
// switched.
absl::Status DmDev::Wait() {
  constexpr base::TimeDelta kMaxWaitTime = base::Seconds(5);
  constexpr base::TimeDelta kRetryDelay = base::Milliseconds(100);
  std::string path = GetPath();

  base::Time startTime = base::Time::Now();
  while (true) {
    if (base::Time::Now() - startTime > kMaxWaitTime)
      return absl::UnavailableError(
          path + " is not available after " +
          std::to_string(kMaxWaitTime.InMilliseconds()) + " ms.");

    if (SwapToolUtil::Get()
            ->PathExists(base::FilePath("/dev/mapper/").Append(name_))
            .ok())
      return absl::OkStatus();

    base::PlatformThread::Sleep(kRetryDelay);
  }
}

std::string DmDev::GetPath() {
  return "/dev/mapper/" + name_;
}

SwapTool::SwapTool(feature::PlatformFeatures* platform_features)
    : platform_features_(platform_features) {}

// Check if swap is already turned on.
absl::StatusOr<bool> SwapTool::IsZramSwapOn() {
  std::string swaps;
  absl::Status status = SwapToolUtil::Get()->ReadFileToString(
      base::FilePath("/proc/swaps"), &swaps);
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
  absl::Status status = SwapToolUtil::Get()->ReadFileToStringWithMaxSize(
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

  uint64_t requested_size_mib = 0;
  if (!absl::SimpleAtoi(buf, &requested_size_mib))
    return absl::OutOfRangeError("Failed to convert " +
                                 std::to_string(requested_size_mib) +
                                 " to 64-bit unsigned integer.");

  if (requested_size_mib == 0)
    LOG(WARNING) << "swap is disabled since " << std::string(kSwapSizeFile)
                 << " contains 0.";

  return requested_size_mib * 1024 * 1024;
}

// Set comp_algorithm if kSwapZramCompAlgorithmFeature is enabled.
void SwapTool::SetCompAlgorithmIfOverriden() {
  std::optional<std::string> comp_algorithm =
      GetFeatureParam(kSwapZramCompAlgorithmFeature, "comp_algorithm");
  if (comp_algorithm.has_value()) {
    LOG(INFO) << "Setting zram comp_algorithm to " << *comp_algorithm;
    absl::Status status = SwapToolUtil::Get()->WriteFile(
        base::FilePath(kZramSysfsDir).Append("comp_algorithm"),
        *comp_algorithm);
    LOG_IF(WARNING, !status.ok()) << status;
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
  LOG_IF(WARNING, !absl::IsNotFound(size_byte.status())) << size_byte.status();

  // 2. Feature
  // First, read /proc/meminfo for MemTotal in kiB.
  absl::StatusOr<base::SystemMemoryInfoKB> meminfo =
      SwapToolUtil::Get()->GetSystemMemoryInfo();
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
  return RoundupMultiple(
      static_cast<uint64_t>((*meminfo).total) * 1024 * multiplier, 4096);
}

// Program /sys/block/zram0/recomp_algorithm.
// For the format of |kSwapRecompAlgorithmFile|, please refer to the
// description in SwapZramSetRecompAlgorithms.
void SwapTool::SetRecompAlgorithms() {
  std::string buf;
  absl::Status status = SwapToolUtil::Get()->ReadFileToString(
      base::FilePath(kSwapRecompAlgorithmFile), &buf);
  std::vector<std::string> algos = base::SplitString(
      buf, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (uint8_t i = 0; i < algos.size(); i++) {
    absl::Status status = SwapToolUtil::Get()->WriteFile(
        base::FilePath(kZramSysfsDir).Append("recomp_algorithm"),
        "algo=" + algos[i] + " priority=" + std::to_string(i + 1));
    LOG_IF(WARNING, !status.ok()) << status;
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
    status = SwapToolUtil::Get()->RunProcessHelper(
        {"/sbin/swapon", kZramDeviceFile});
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

// If we're unable to setup writeback just make sure we clean up any
// mounts.
// Devices are cleanup while class instances are released.
// Errors happenes during cleanup will be logged.
void SwapTool::CleanupWriteback() {
  absl::Status status = absl::OkStatus();

  status = SwapToolUtil::Get()->Umount(kZramWritebackIntegrityMount);
  LOG_IF(ERROR, !status.ok()) << status;

  status = SwapToolUtil::Get()->DeleteFile(
      base::FilePath(kZramWritebackIntegrityMount));
  LOG_IF(ERROR, !status.ok()) << status;
}

// Check if zram writeback can be used on the system.
absl::Status SwapTool::ZramWritebackPrerequisiteCheck(uint32_t size) {
  absl::Status status = absl::OkStatus();

  // Don't allow |size| less than 128MiB or more than 6GiB to be configured.
  constexpr uint32_t kZramWritebackMinSize = 128;
  constexpr uint32_t kZramWritebackMaxSize = 6144;
  if (size < kZramWritebackMinSize || size > kZramWritebackMaxSize)
    return absl::InvalidArgumentError("Invalid size specified.");

  // kZramBackingDevice must contains none, no writeback is setup before.
  std::string backing_dev;
  status = SwapToolUtil::Get()->ReadFileToString(
      base::FilePath(kZramBackingDevice), &backing_dev);
  if (!status.ok())
    return status;
  base::TrimWhitespaceASCII(backing_dev, base::TRIM_ALL, &backing_dev);
  if (backing_dev != "none")
    return absl::AlreadyExistsError(
        "Zram already has a backing device assigned.");

  // kZramWritebackIntegrityMount must not be mounted.
  // rmdir(2) will return -EBUSY if the target is mounted.
  // DeleteFile returns absl::OkStatus() if the target does not exist.
  status = SwapToolUtil::Get()->DeleteFile(
      base::FilePath(kZramWritebackIntegrityMount));

  return status;
}

absl::Status SwapTool::GetZramWritebackInfo(uint32_t size) {
  absl::Status status = absl::OkStatus();

  // Read stateful partition file system statistics using statfs.
  // f_blocks is total data blocks in file system.
  // f_bfree is free blocks in file system.
  // f_bsize is the optimal transfer block size.
  absl::StatusOr<struct statfs> stateful_statfs =
      SwapToolUtil::Get()->GetStatfs(kStatefulPartitionDir);
  if (!stateful_statfs.ok())
    return stateful_statfs.status();

  // Never allow swapping to disk when the overall free diskspace is less
  // than 15% of the overall capacity.
  constexpr int kMinFreeStatefulPct = 15;
  uint64_t stateful_free_pct =
      100 * (*stateful_statfs).f_bfree / (*stateful_statfs).f_blocks;
  if (stateful_free_pct < kMinFreeStatefulPct)
    return absl::ResourceExhaustedError(
        "Zram writeback cannot be enabled free disk space" +
        std::to_string(stateful_free_pct) + "% is less than the minimum 15%");

  stateful_block_size_ = (*stateful_statfs).f_bsize;
  wb_nr_blocks_ = size * kMiB / stateful_block_size_;
  uint64_t wb_pct_of_stateful =
      wb_nr_blocks_ * 100 / (*stateful_statfs).f_bfree;

  // Only allow 15% of the free diskspace for swap writeback by maximum.
  if (wb_pct_of_stateful > kMinFreeStatefulPct) {
    uint64_t old_size = size;
    wb_nr_blocks_ = kMinFreeStatefulPct * (*stateful_statfs).f_bfree / 100;
    size = wb_nr_blocks_ * stateful_block_size_ / kMiB;
    LOG(WARNING) << "Zram writeback, requested size of " << old_size << " is "
                 << wb_pct_of_stateful
                 << "% of the free disk space. Size will be reduced to " << size
                 << "MiB";
  }

  wb_size_bytes_ = RoundupMultiple(size * kMiB, stateful_block_size_);
  // Because we rounded up writeback_size bytes recalculate the number of blocks
  // used.
  wb_nr_blocks_ = wb_size_bytes_ / stateful_block_size_;

  return absl::OkStatus();
}

absl::Status SwapTool::CreateDmDevicesAndEnableWriteback() {
  absl::Status status = absl::OkStatus();

  // Create the actual writeback space on the stateful partition.
  constexpr char kZramWritebackBackFileName[] = "zram_writeback.swap";
  ScopedFilePath scoped_filepath(
      base::FilePath(kStatefulPartitionDir).Append(kZramWritebackBackFileName));
  status = SwapToolUtil::Get()->WriteFile(scoped_filepath.get(), std::string());
  if (!status.ok())
    return status;
  status =
      SwapToolUtil::Get()->Fallocate(scoped_filepath.get(), wb_size_bytes_);
  if (!status.ok())
    return status;

  // Create writeback loop device.
  // See drivers/block/loop.c:230
  // We support direct I/O only if lo_offset is aligned with the
  // logical I/O size of backing device, and the logical block
  // size of loop is bigger than the backing device's and the loop
  // needn't transform transfer.
  auto writeback_loop = LoopDev::Create(scoped_filepath.get().value(), true,
                                        stateful_block_size_);
  if (!writeback_loop.ok())
    return writeback_loop.status();
  std::string writeback_loop_path = (*writeback_loop)->GetPath();

  // Create and mount ramfs for integrity loop device back file.
  status = SwapToolUtil::Get()->CreateDirectory(
      base::FilePath(kZramWritebackIntegrityMount));
  if (!status.ok())
    return status;
  status = SwapToolUtil::Get()->SetPosixFilePermissions(
      base::FilePath(kZramWritebackIntegrityMount), 0700);
  if (!status.ok())
    return status;
  status =
      SwapToolUtil::Get()->Mount("none", kZramWritebackIntegrityMount, "ramfs",
                                 0, "noexec,nosuid,noatime,mode=0700");
  if (!status.ok())
    return status;

  // Create integrity loop device.
  // See drivers/md/dm-integrity.c and
  // https://docs.kernel.org/admin-guide/device-mapper/dm-integrity.html
  // In direct write mode, The size of dm-integrity is data(tag) area + initial
  // segment.
  // The size of data(tag) area is (number of blocks in wb device) *
  // (tag size), and then roundup with the size of dm-integrity buffer. The
  // default number of sector in a dm-integrity buffer is 128 so the size is
  // 65536 bytes.
  // The size of initial segment is (superblock size == 4KiB) + (size of
  // journal). dm-integrity requires at least one journal section even with
  // direct write mode. As for now, the size of a single journal section is
  // 167936 bytes (328 sectors)

  // AES-GCM uses a fixed 12 byte IV. The other 12 bytes are auth tag.
  constexpr size_t kDmIntegrityTagSize = 24;
  constexpr size_t kDmIntegrityBufSize = 65536;
  constexpr size_t kJournalSectionSize = kSectorSize * 328;
  constexpr size_t kSuperblockSize = 4096;
  constexpr size_t kInitialSegmentSize = kSuperblockSize + kJournalSectionSize;

  size_t data_area_size =
      RoundupMultiple(wb_nr_blocks_ * kDmIntegrityTagSize, kDmIntegrityBufSize);

  size_t integrity_size_bytes = data_area_size + kInitialSegmentSize;
  // To be safe, in case the size of dm-integrity increases in the future
  // development, roundup it with MiB.
  integrity_size_bytes = RoundupMultiple(integrity_size_bytes, kMiB);

  constexpr char kZramIntegrityBackFileName[] = "zram_integrity.swap";
  scoped_filepath = ScopedFilePath(base::FilePath(kZramWritebackIntegrityMount)
                                       .Append(kZramIntegrityBackFileName));
  // Truncate the file to the length of |integrity_size_bytes| by filling with
  // 0s.
  status = SwapToolUtil::Get()->WriteFile(scoped_filepath.get(),
                                          std::string(integrity_size_bytes, 0));
  if (!status.ok())
    return status;

  auto integrity_loop = LoopDev::Create(scoped_filepath.get().value());
  if (!integrity_loop.ok())
    return integrity_loop.status();
  std::string integrity_loop_path = (*integrity_loop)->GetPath();

  // Create a dm-integrity device to use with dm-crypt.
  // For the table format, refer to
  // https://wiki.gentoo.org/wiki/Device-mapper#Integrity
  std::string table_fmt = base::StringPrintf(
      "0 %" PRId64 " integrity %s 0 %zu D 4 block_size:%" PRId64
      " meta_device:%s journal_sectors:1 buffer_sectors:%zu",
      wb_size_bytes_ / kSectorSize, writeback_loop_path.c_str(),
      kDmIntegrityTagSize, stateful_block_size_, integrity_loop_path.c_str(),
      kDmIntegrityBufSize / kSectorSize);
  auto integrity_dm = DmDev::Create(kZramIntegrityName, table_fmt);
  if (!integrity_dm.ok())
    return integrity_dm.status();

  // Create a dm-crypt device for writeback.
  absl::StatusOr<std::string> rand_hex32 =
      SwapToolUtil::Get()->GenerateRandHex(32);
  if (!rand_hex32.ok())
    return rand_hex32.status();

  table_fmt = base::StringPrintf(
      "0 %" PRId64
      " crypt capi:gcm(aes)-random %s 0 /dev/mapper/%s 0 4 allow_discards "
      "submit_from_crypt_cpus sector_size:%" PRId64 " integrity:%zu:aead",
      wb_size_bytes_ / kSectorSize, (*rand_hex32).c_str(), kZramIntegrityName,
      stateful_block_size_, kDmIntegrityTagSize);

  auto writeback_dm = DmDev::Create(kZramWritebackName, table_fmt);
  if (!writeback_dm.ok())
    return writeback_dm.status();

  // Set up dm-crypt device as the zram writeback backing device.
  return SwapToolUtil::Get()->WriteFile(base::FilePath(kZramBackingDevice),
                                        (*writeback_dm)->GetPath());
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
  if (!SwapToolUtil::Get()->RunProcessHelper({"/sbin/modprobe", "zram"}).ok())
    LOG(WARNING) << "modprobe zram failed (compiled?)";

  // Set zram recompress algorithm if user has config.
  SetRecompAlgorithms();

  // Set zram compress algorithm if feature is available.
  SetCompAlgorithmIfOverriden();

  // Set zram size.
  LOG(INFO) << "Setting zram disksize to " << *size_byte << " bytes";
  status = SwapToolUtil::Get()->WriteFile(
      base::FilePath(kZramSysfsDir).Append("disksize"),
      std::to_string(*size_byte));
  if (!status.ok())
    return status;

  // Set swap area.
  status =
      SwapToolUtil::Get()->RunProcessHelper({"/sbin/mkswap", kZramDeviceFile});
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
  absl::Status status = SwapToolUtil::Get()->RunProcessHelper(
      {"/sbin/swapoff", "-v", kZramDeviceFile});
  if (!status.ok())
    return status;

  // When we start up, we try to configure zram0, but it doesn't like to
  // be reconfigured on the fly.  Reset it so we can changes its params.
  // If there was a backing device being used, it will be automatically
  // removed because after it's created it was removed with deferred remove.
  return SwapToolUtil::Get()->WriteFile(
      base::FilePath(kZramSysfsDir).Append("reset"), "1");
}

// Set zram disksize in MiB.
// If `size` equals 0, set zram size file to the default value.
// If `size` is negative, set zram size file to 0. Swap is disabled if zram size
// file contains 0.
absl::Status SwapTool::SwapSetSize(int32_t size) {
  // Remove kSwapSizeFile so SwapStart will use default size for zram.
  if (size == 0) {
    return SwapToolUtil::Get()->DeleteFile(base::FilePath(kSwapSizeFile));
  } else if (size < 0) {
    size = 0;
  } else if (size < 128 || size > 65000) {
    return absl::InvalidArgumentError("Size is not between 128 and 65000 MiB.");
  }

  return SwapToolUtil::Get()->WriteFile(base::FilePath(kSwapSizeFile),
                                        std::to_string(size));
}

absl::Status SwapTool::SwapSetSwappiness(uint32_t swappiness) {
  // Only allow swappiness between 0 and 100.
  if (swappiness > 100)
    return absl::OutOfRangeError("Invalid swappiness " +
                                 std::to_string(swappiness));

  return SwapToolUtil::Get()->WriteFile(
      base::FilePath("/proc/sys/vm/swappiness"), std::to_string(swappiness));
}

std::string SwapTool::SwapStatus() {
  std::stringstream output;
  std::string tmp;

  // Show general swap info first.
  if (SwapToolUtil::Get()
          ->ReadFileToString(base::FilePath("/proc/swaps"), &tmp)
          .ok())
    output << tmp;

  // Show tunables.
  if (SwapToolUtil::Get()
          ->ReadFileToString(base::FilePath("/proc/sys/vm/min_filelist_kbytes"),
                             &tmp)
          .ok())
    output << "min_filelist_kbytes (KiB): " + tmp;
  if (SwapToolUtil::Get()
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

      if (SwapToolUtil::Get()
              ->ReadFileToString(zram_sysfs.Append(name), &tmp)
              .ok() &&
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

absl::Status SwapTool::SwapZramEnableWriteback(uint32_t size) {
  absl::Status status = absl::OkStatus();

  status = ZramWritebackPrerequisiteCheck(size);
  if (!status.ok())
    return status;

  status = GetZramWritebackInfo(size);
  if (!status.ok())
    return status;

  status = CreateDmDevicesAndEnableWriteback();
  if (!status.ok()) {
    CleanupWriteback();
    return status;
  }

  LOG(INFO) << "Enabled writeback with size " +
                   std::to_string(wb_size_bytes_ / kMiB) + "MiB";

  return absl::OkStatus();
}

absl::Status SwapTool::SwapZramSetWritebackLimit(uint32_t num_pages) {
  base::FilePath filepath =
      base::FilePath(kZramSysfsDir).Append("writeback_limit_enable");

  absl::Status status = SwapToolUtil::Get()->WriteFile(filepath, "1");
  if (!status.ok())
    return status;

  filepath = base::FilePath(kZramSysfsDir).Append("writeback_limit");

  return SwapToolUtil::Get()->WriteFile(filepath, std::to_string(num_pages));
}

absl::Status SwapTool::SwapZramMarkIdle(uint32_t age_seconds) {
  const auto age = base::Seconds(age_seconds);

  // Only allow marking pages as idle between 0 sec and 30 days.
  if (age > kMaxIdleAge)
    return absl::OutOfRangeError("Invalid age " + std::to_string(age_seconds));

  base::FilePath filepath = base::FilePath(kZramSysfsDir).Append("idle");
  return SwapToolUtil::Get()->WriteFile(filepath,
                                        std::to_string(age.InSeconds()));
}

absl::Status SwapTool::InitiateSwapZramWriteback(ZramWritebackMode mode) {
  base::FilePath filepath = base::FilePath(kZramSysfsDir).Append("writeback");
  std::string mode_str;
  if (mode == WRITEBACK_IDLE) {
    mode_str = "idle";
  } else if (mode == WRITEBACK_HUGE) {
    mode_str = "huge";
  } else if (mode == WRITEBACK_HUGE_IDLE) {
    mode_str = "huge_idle";
  } else {
    return absl::InvalidArgumentError("Invalid mode");
  }

  return SwapToolUtil::Get()->WriteFile(filepath, mode_str);
}

absl::Status SwapTool::MGLRUSetEnable(uint8_t value) {
  return SwapToolUtil::Get()->WriteFile(
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

  return SwapToolUtil::Get()->WriteFile(filepath, ss.str());
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
    return SwapToolUtil::Get()->DeleteFile(
        base::FilePath(kSwapRecompAlgorithmFile));

  const std::string joined = base::JoinString(algos, " ");
  return SwapToolUtil::Get()->WriteFile(
      base::FilePath(kSwapRecompAlgorithmFile), joined);
}

}  // namespace swap_management
