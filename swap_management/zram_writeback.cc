// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/utils.h"
#include "swap_management/zram_stats.h"
#include "swap_management/zram_writeback.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <absl/cleanup/cleanup.h>
#include <absl/status/status.h>
#include <absl/strings/numbers.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/timer/timer.h>
#include <brillo/errors/error.h>

namespace swap_management {

namespace {

constexpr char kZramWritebackName[] = "zram-writeback";
constexpr char kZramIntegrityName[] = "zram-integrity";
constexpr char kZramWritebackIntegrityMount[] = "/run/zram-integrity";
constexpr char kZramBackingDevice[] = "/sys/block/zram0/backing_dev";
constexpr char kStatefulPartitionDir[] =
    "/mnt/stateful_partition/unencrypted/userspace_swap.tmp";
constexpr uint32_t kSectorSize = 512;
constexpr base::TimeDelta kMaxIdleAge = base::Days(30);

ZramWriteback* inst_ = nullptr;

base::RepeatingTimer writeback_timer_;

absl::StatusOr<std::string> WritebackModeToName(ZramWritebackMode mode) {
  if (mode == WRITEBACK_IDLE)
    return "idle";
  else if (mode == WRITEBACK_HUGE)
    return "huge";
  else if (mode == WRITEBACK_HUGE_IDLE)
    return "huge_idle";
  else
    return absl::InvalidArgumentError("Invalid mode");
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
  absl::Status status = Utils::Get()->RunProcessHelper(command, &loop_dev_path);
  if (!status.ok())
    return status;
  base::TrimWhitespaceASCII(loop_dev_path, base::TRIM_ALL, &loop_dev_path);

  return std::unique_ptr<LoopDev>(new LoopDev(loop_dev_path));
}

LoopDev::~LoopDev() {
  absl::Status status = absl::OkStatus();

  if (!path_.empty()) {
    status = Utils::Get()->RunProcessHelper({"/sbin/losetup", "-d", path_});
    LOG_IF(ERROR, !status.ok()) << "Can not detach loop device: " << status;
    path_.clear();
  }
}

std::string LoopDev::GetPath() {
  return path_;
}

absl::StatusOr<std::unique_ptr<DmDev>> DmDev::Create(
    const std::string& name, const std::string& table_fmt) {
  absl::Status status = absl::OkStatus();

  status = Utils::Get()->RunProcessHelper(
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
    status = Utils::Get()->RunProcessHelper(
        {"/sbin/dmsetup", "remove", "--deferred", name_});
    LOG_IF(ERROR, !status.ok()) << "Can not remove dm device: " << status;
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

    if (Utils::Get()
            ->PathExists(base::FilePath("/dev/mapper/").Append(name_))
            .ok())
      return absl::OkStatus();

    base::PlatformThread::Sleep(kRetryDelay);
  }
}

std::string DmDev::GetPath() {
  return "/dev/mapper/" + name_;
}

ZramWriteback* ZramWriteback::Get() {
  [[maybe_unused]] static bool created = []() -> bool {
    if (!inst_)
      inst_ = new ZramWriteback;
    return true;
  }();

  return inst_;
}

// If we're unable to setup writeback just make sure we clean up any
// mounts.
// Devices are cleanup while class instances are released.
// Errors happenes during cleanup will be logged.
void ZramWriteback::Cleanup() {
  absl::Status status = absl::OkStatus();

  status = Utils::Get()->Umount(kZramWritebackIntegrityMount);
  LOG_IF(ERROR, !status.ok())
      << "Can not umount " << kZramWritebackIntegrityMount << ": " << status;

  status =
      Utils::Get()->DeleteFile(base::FilePath(kZramWritebackIntegrityMount));
  LOG_IF(ERROR, !status.ok())
      << "Can not remove " << kZramWritebackIntegrityMount << ": " << status;
}

// Check if zram writeback can be used on the system.
absl::Status ZramWriteback::PrerequisiteCheck(uint32_t size) {
  absl::Status status = absl::OkStatus();

  // Don't allow |size| less than 128MiB or more than 6GiB to be configured.
  constexpr uint32_t kZramWritebackMinSize = 128;
  constexpr uint32_t kZramWritebackMaxSize = 6144;
  if (size < kZramWritebackMinSize || size > kZramWritebackMaxSize)
    return absl::InvalidArgumentError("Invalid size specified.");

  // kZramBackingDevice must contains none, no writeback is setup before.
  std::string backing_dev;
  status = Utils::Get()->ReadFileToString(base::FilePath(kZramBackingDevice),
                                          &backing_dev);
  if (!status.ok())
    return status;
  base::TrimWhitespaceASCII(backing_dev, base::TRIM_ALL, &backing_dev);
  if (backing_dev != "none")
    return absl::AlreadyExistsError(
        "Zram already has a backing device assigned.");

  // kZramWritebackIntegrityMount must not be mounted.
  // rmdir(2) will return -EBUSY if the target is mounted.
  // DeleteFile returns absl::OkStatus() if the target does not exist.
  status =
      Utils::Get()->DeleteFile(base::FilePath(kZramWritebackIntegrityMount));

  return status;
}

absl::Status ZramWriteback::GetWritebackInfo(uint32_t size) {
  absl::Status status = absl::OkStatus();

  // Read stateful partition file system statistics using statfs.
  // f_blocks is total data blocks in file system.
  // f_bfree is free blocks in file system.
  // f_bsize is the optimal transfer block size.
  absl::StatusOr<struct statfs> stateful_statfs =
      Utils::Get()->GetStatfs(kStatefulPartitionDir);
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

  wb_size_bytes_ =
      Utils::Get()->RoundupMultiple(size * kMiB, stateful_block_size_);
  // Because we rounded up writeback_size bytes recalculate the number of blocks
  // used.
  wb_nr_blocks_ = wb_size_bytes_ / stateful_block_size_;

  return absl::OkStatus();
}

absl::Status ZramWriteback::CreateDmDevicesAndEnableWriteback() {
  absl::Status status = absl::OkStatus();

  // Create the actual writeback space on the stateful partition.
  constexpr char kZramWritebackBackFileName[] = "zram_writeback.swap";
  ScopedFilePath scoped_filepath(
      base::FilePath(kStatefulPartitionDir).Append(kZramWritebackBackFileName));
  status = Utils::Get()->WriteFile(scoped_filepath.get(), std::string());
  if (!status.ok())
    return status;
  status = Utils::Get()->Fallocate(scoped_filepath.get(), wb_size_bytes_);
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
  status = Utils::Get()->CreateDirectory(
      base::FilePath(kZramWritebackIntegrityMount));
  if (!status.ok())
    return status;
  status = Utils::Get()->SetPosixFilePermissions(
      base::FilePath(kZramWritebackIntegrityMount), 0700);
  if (!status.ok())
    return status;
  status = Utils::Get()->Mount("none", kZramWritebackIntegrityMount, "ramfs", 0,
                               "noexec,nosuid,noatime,mode=0700");
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

  size_t data_area_size = Utils::Get()->RoundupMultiple(
      wb_nr_blocks_ * kDmIntegrityTagSize, kDmIntegrityBufSize);

  size_t integrity_size_bytes = data_area_size + kInitialSegmentSize;
  // To be safe, in case the size of dm-integrity increases in the future
  // development, roundup it with MiB.
  integrity_size_bytes =
      Utils::Get()->RoundupMultiple(integrity_size_bytes, kMiB);

  constexpr char kZramIntegrityBackFileName[] = "zram_integrity.swap";
  scoped_filepath = ScopedFilePath(base::FilePath(kZramWritebackIntegrityMount)
                                       .Append(kZramIntegrityBackFileName));
  // Truncate the file to the length of |integrity_size_bytes| by filling with
  // 0s.
  status = Utils::Get()->WriteFile(scoped_filepath.get(),
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
  absl::StatusOr<std::string> rand_hex32 = Utils::Get()->GenerateRandHex(32);
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
  return Utils::Get()->WriteFile(base::FilePath(kZramBackingDevice),
                                 (*writeback_dm)->GetPath());
}

absl::Status ZramWriteback::EnableWriteback(uint32_t size) {
  absl::Status status = absl::OkStatus();

  status = PrerequisiteCheck(size);
  if (!status.ok())
    return status;

  status = GetWritebackInfo(size);
  if (!status.ok())
    return status;

  status = CreateDmDevicesAndEnableWriteback();
  if (!status.ok()) {
    Cleanup();
    return status;
  }

  LOG(INFO) << "Enabled writeback with size " +
                   std::to_string(wb_size_bytes_ / kMiB) + "MiB";

  return absl::OkStatus();
}

absl::Status ZramWriteback::SetWritebackLimit(uint32_t num_pages) {
  base::FilePath filepath =
      base::FilePath(kZramSysfsDir).Append("writeback_limit_enable");

  absl::Status status = Utils::Get()->WriteFile(filepath, "1");
  if (!status.ok())
    return status;

  filepath = base::FilePath(kZramSysfsDir).Append("writeback_limit");

  return Utils::Get()->WriteFile(filepath, std::to_string(num_pages));
}

absl::Status ZramWriteback::MarkIdle(uint32_t age_seconds) {
  const auto age = base::Seconds(age_seconds);

  // Only allow marking pages as idle between 0 sec and 30 days.
  if (age > kMaxIdleAge)
    return absl::OutOfRangeError("Invalid age " + std::to_string(age_seconds));

  base::FilePath filepath = base::FilePath(kZramSysfsDir).Append("idle");
  return Utils::Get()->WriteFile(filepath, std::to_string(age.InSeconds()));
}

absl::Status ZramWriteback::InitiateWriteback(ZramWritebackMode mode) {
  base::FilePath filepath = base::FilePath(kZramSysfsDir).Append("writeback");
  absl::StatusOr<std::string> mode_str = WritebackModeToName(mode);
  if (!mode_str.ok())
    return mode_str.status();

  return Utils::Get()->WriteFile(filepath, *mode_str);
}

ZramWriteback::~ZramWriteback() {
  writeback_timer_.Stop();
  Cleanup();
}

absl::Status ZramWriteback::SetZramWritebackConfigIfOverriden(
    const std::string& key, const std::string& value) {
  if (key == "backing_dev_size_mib") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.backing_dev_size_mib = *buf;
  } else if (key == "periodic_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.periodic_time = base::Seconds(*buf);
  } else if (key == "backoff_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.backoff_time = base::Seconds(*buf);
  } else if (key == "min_pages") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.min_pages = *buf;
  } else if (key == "max_pages") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.max_pages = *buf;
  } else if (key == "writeback_huge") {
    auto buf = Utils::Get()->SimpleAtob(value);
    if (!buf.ok())
      return buf.status();
    params_.writeback_huge = *buf;
  } else if (key == "writeback_huge_idle") {
    auto buf = Utils::Get()->SimpleAtob(value);
    if (!buf.ok())
      return buf.status();
    params_.writeback_huge_idle = *buf;
  } else if (key == "writeback_idle") {
    auto buf = Utils::Get()->SimpleAtob(value);
    if (!buf.ok())
      return buf.status();
    params_.writeback_idle = *buf;
  } else if (key == "idle_min_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.idle_min_time = base::Seconds(*buf);
  } else if (key == "idle_max_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.idle_max_time = base::Seconds(*buf);
  } else {
    return absl::InvalidArgumentError("Unknown key " + key);
  }

  return absl::OkStatus();
}

absl::StatusOr<uint64_t> ZramWriteback::GetAllowedWritebackLimit() {
  // We need to decide how many pages we will want to write back total, this
  // includes huge and idle if they are both enabled. The calculation is based
  // on zram utilization, writeback utilization, and memory pressure.
  uint64_t num_pages = 0;

  absl::StatusOr<ZramMmStat> zram_mm_stat = GetZramMmStat();
  if (!zram_mm_stat.ok())
    return zram_mm_stat.status();
  absl::StatusOr<ZramBdStat> zram_bd_stat = GetZramBdStat();
  if (!zram_bd_stat.ok())
    return zram_bd_stat.status();

  // All calculations are performed in basis points, 100 bps = 1.00%. The
  // number of pages allowed to be written back follows a simple linear
  // relationship. The allowable range is [min_pages, max_pages], and the
  // writeback limit will be the (zram utilization) * the range, that is, the
  // more zram we're using the more we're going to allow to be written back.
  constexpr uint32_t kBps = 100 * 100;
  uint64_t pages_currently_written_back = (*zram_bd_stat).bd_count;
  uint64_t zram_utilization_bps =
      (((*zram_mm_stat).orig_data_size / kPageSize) * kBps) / zram_nr_pages_;
  num_pages = zram_utilization_bps * params_.max_pages / kBps;

  // And try to limit it to the approximate number of free backing device
  // pages (if it's less).
  uint64_t free_bd_pages =
      (wb_size_bytes_ / kPageSize) - pages_currently_written_back;
  num_pages = std::min(num_pages, free_bd_pages);

  // Finally enforce the limits, we won't even attempt writeback if we
  // cannot writeback at least the min, and we will cap to the max if it's
  // greater.
  num_pages = std::min(num_pages, params_.max_pages);
  if (num_pages < params_.min_pages)
    // Configured to not writeback fewer than configured min_pages.
    return 0;

  return num_pages;
}

std::optional<base::TimeDelta> ZramWriteback::GetCurrentWritebackIdleTime() {
  if (!params_.writeback_idle)
    return std::nullopt;

  absl::StatusOr<base::SystemMemoryInfoKB> meminfo =
      Utils::Get()->GetSystemMemoryInfo();
  if (!meminfo.ok()) {
    LOG(ERROR) << "Can not read meminfo: " << meminfo.status();
    return std::nullopt;
  }

  // Stay between idle_(min|max)_time.
  uint64_t min_sec = params_.idle_min_time.InSeconds();
  uint64_t max_sec = params_.idle_max_time.InSeconds();
  double mem_utilization =
      (1.0 - (static_cast<double>((*meminfo).available) / (*meminfo).total));

  // Exponentially decay the writeback age vs. memory utilization. The reason
  // we choose exponential decay is because we want to do as little work as
  // possible when the system is under very low memory pressure. As pressure
  // increases we want to start aggressively shrinking our idle age to force
  // newer pages to be written back.
  constexpr double kLambda = 5;
  uint64_t age_sec =
      (max_sec - min_sec) * pow(M_E, -kLambda * mem_utilization) + min_sec;

  return base::Seconds(age_sec);
}

// Read the actual programmed writeback_limit.
absl::StatusOr<uint64_t> ZramWriteback::GetWritebackLimit() {
  std::string buf;
  absl::Status status = Utils::Get()->ReadFileToString(
      base::FilePath(kZramSysfsDir).Append("writeback_limit"), &buf);
  if (!status.ok())
    return status;

  return Utils::Get()->SimpleAtoi<uint64_t>(buf);
}

void ZramWriteback::PeriodicWriteback() {
  // Is writeback ongoing?
  if (is_currently_writing_back_)
    return;
  absl::Cleanup cleanup = [&] { is_currently_writing_back_ = false; };

  // Did we writeback too recently?
  if (last_writeback_ != base::Time::Min()) {
    const auto time_since_writeback = base::Time::Now() - last_writeback_;
    if (time_since_writeback < params_.backoff_time)
      return;
  }

  absl::StatusOr<uint64_t> num_pages = GetAllowedWritebackLimit();
  if (!num_pages.ok() || *num_pages == 0) {
    LOG_IF(ERROR, !num_pages.ok())
        << "Can not get allowed writeback_limit: " << num_pages.status();
    return;
  }
  absl::Status status = SetWritebackLimit(*num_pages);
  if (!status.ok()) {
    LOG(ERROR) << "Can not set zram writeback_limit: " << status;
    return;
  }

  // If no writeback quota available then do not writeback.
  absl::StatusOr<uint64_t> writeback_limit = GetWritebackLimit();
  if (!writeback_limit.ok() || *writeback_limit == 0) {
    LOG_IF(ERROR, !writeback_limit.ok())
        << "Can not read zram writeback_limit: " << writeback_limit.status();
    return;
  }

  // We started on huge idle page writeback, then idle, then huge pages, if
  // enabled accordingly.
  ZramWritebackMode current_writeback_mode = WRITEBACK_HUGE_IDLE;
  while (current_writeback_mode != WRITEBACK_NONE) {
    // Do enable writeback at current mode?
    if ((current_writeback_mode == WRITEBACK_HUGE_IDLE &&
         params_.writeback_huge_idle) ||
        (current_writeback_mode == WRITEBACK_IDLE && params_.writeback_idle) ||
        (current_writeback_mode == WRITEBACK_HUGE && params_.writeback_huge)) {
      // If we currently working on huge_idle or idle mode, mark idle for pages.
      if (current_writeback_mode == WRITEBACK_HUGE_IDLE ||
          current_writeback_mode == WRITEBACK_IDLE) {
        std::optional<base::TimeDelta> idle_age = GetCurrentWritebackIdleTime();
        if (!idle_age.has_value()) {
          // Failed to calculate idle age, directly move to huge page.
          current_writeback_mode = WRITEBACK_HUGE;
          continue;
        }
        status = MarkIdle((*idle_age).InSeconds());
        if (!status.ok()) {
          LOG(ERROR) << "Can not mark zram idle:" << status;
          return;
        }
      }

      // Then we initiate writeback.
      status = InitiateWriteback(current_writeback_mode);
      // It could fail because of depleted writeback limit quota.
      absl::StatusOr<uint64_t> writeback_limit_after = GetWritebackLimit();
      if (!writeback_limit_after.ok()) {
        LOG(ERROR) << "Can not read zram writeback_limit: "
                   << writeback_limit_after.status();
        return;
      }
      if (!status.ok() && *writeback_limit_after != 0) {
        LOG(ERROR) << "Can not initiate zram writeback: " << status;
        return;
      }
      last_writeback_ = base::Time::Now();

      // Log the number of writeback pages.
      int64_t num_wb_pages = *writeback_limit - *writeback_limit_after;
      if (num_wb_pages > 0) {
        absl::StatusOr<std::string> mode =
            WritebackModeToName(current_writeback_mode);
        if (mode.ok())
          LOG(INFO) << "zram writeback " << num_wb_pages << " " << *mode
                    << " pages.";
      }

      // Update writeback_limit for next mode, or exit if no more quota.
      if (*writeback_limit_after == 0)
        return;
      writeback_limit = writeback_limit_after;
    }

    // Move to the next stage.
    if (current_writeback_mode == WRITEBACK_HUGE_IDLE)
      current_writeback_mode = WRITEBACK_IDLE;
    else if (current_writeback_mode == WRITEBACK_IDLE)
      current_writeback_mode = WRITEBACK_HUGE;
    else
      current_writeback_mode = WRITEBACK_NONE;
  }
}

absl::Status ZramWriteback::Start() {
  LOG(INFO) << "Zram writeback params: " << params_;

  // Basic sanity check on our configuration.
  if (!params_.writeback_huge && !params_.writeback_idle &&
      !params_.writeback_huge_idle)
    return absl::InvalidArgumentError("No setup for writeback page type.");

  // We don't start again if writeback is enabled.
  std::string buf;
  absl::Status status =
      Utils::Get()->ReadFileToString(base::FilePath(kZramBackingDevice), &buf);
  if (!status.ok())
    return status;
  base::TrimWhitespaceASCII(buf, base::TRIM_ALL, &buf);
  if (buf.empty())
    return absl::InvalidArgumentError(std::string(kZramBackingDevice) +
                                      " is empty.");
  if (buf != "none") {
    LOG(WARNING) << "Zram writeback is already enabled.";
    return absl::OkStatus();
  }

  status = EnableWriteback(params_.backing_dev_size_mib);
  if (!status.ok())
    return status;

  status = Utils::Get()->ReadFileToString(
      base::FilePath(kZramSysfsDir).Append("disksize"), &buf);
  if (!status.ok())
    return status;
  absl::StatusOr<uint64_t> zram_disksize_byte =
      Utils::Get()->SimpleAtoi<uint64_t>(buf);
  if (!zram_disksize_byte.ok())
    return zram_disksize_byte.status();
  zram_nr_pages_ = *zram_disksize_byte / kPageSize;

  // Start periodic writeback.
  writeback_timer_.Start(FROM_HERE, params_.periodic_time,
                         base::BindRepeating(&ZramWriteback::PeriodicWriteback,
                                             weak_factory_.GetWeakPtr()));

  return absl::OkStatus();
}

void ZramWriteback::Stop() {
  writeback_timer_.Stop();
}

}  // namespace swap_management
