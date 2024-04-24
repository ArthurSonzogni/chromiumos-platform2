// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_ZRAM_WRITEBACK_H_
#define SWAP_MANAGEMENT_ZRAM_WRITEBACK_H_

#include "swap_management/utils.h"

#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <utility>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>

namespace swap_management {

class LoopDev {
 public:
  LoopDev() = delete;
  LoopDev(const LoopDev&) = delete;
  LoopDev& operator=(const LoopDev&) = delete;
  ~LoopDev();

  static absl::StatusOr<std::unique_ptr<LoopDev>> Create(
      const std::string& path);
  static absl::StatusOr<std::unique_ptr<LoopDev>> Create(
      const std::string& path, bool direct_io, uint32_t sector_size);

  std::string GetPath();

 private:
  explicit LoopDev(std::string path) : path_(path) {}

  std::string path_;
};

class DmDev {
 public:
  DmDev() = delete;
  DmDev(const DmDev&) = delete;
  DmDev& operator=(const DmDev&) = delete;
  ~DmDev();

  static absl::StatusOr<std::unique_ptr<DmDev>> Create(
      const std::string& name, const std::string& table_fmt);

  std::string GetPath();

 private:
  explicit DmDev(std::string name) : name_(name) {}

  std::string name_;

  absl::Status Wait();
};

class ZramWriteback {
 public:
  ZramWriteback& operator=(const ZramWriteback&) = delete;
  ZramWriteback(const ZramWriteback&) = delete;

  static ZramWriteback* Get();

  absl::Status SetZramWritebackConfigIfOverriden(const std::string& key,
                                                 const std::string& value);
  absl::Status Start();
  void Stop();

  // TODO(ctshao): Move to private once the finch experiment is done:
  // cl/459290244
  absl::Status EnableWriteback(uint32_t size_mb);
  absl::Status SetWritebackLimit(uint32_t num_pages);
  absl::Status InitiateWriteback(ZramWritebackMode mode);

 private:
  ZramWriteback() = default;

  ~ZramWriteback();

  friend class MockZramWriteback;

  // There are only one zram writeback instance in current setup.
  friend ZramWriteback** GetSingleton<ZramWriteback>();

  void Cleanup();
  absl::Status PrerequisiteCheck(uint32_t size);
  absl::Status GetWritebackInfo(uint32_t size);
  absl::Status CreateDmDevicesAndEnableWriteback();

  absl::StatusOr<uint64_t> GetAllowedWritebackLimit();
  absl::StatusOr<uint64_t> GetWritebackLimit();
  void PeriodicWriteback();

  struct ZramWritebackParams {
    uint32_t backing_dev_size_mib = 1024;
    base::TimeDelta periodic_time = base::Hours(1);
    base::TimeDelta backoff_time = base::Minutes(10);
    uint64_t min_pages = ((5 << 20) / kPageSize);   /* 5MiB worth of pages */
    uint64_t max_pages = ((300 << 20) / kPageSize); /* 300MiB worth of pages */
    uint64_t max_pages_per_day =
        ((1 << 30) / kPageSize); /* 1GiB worth of pages */
    bool writeback_huge_idle = true;
    bool writeback_idle = true;
    bool writeback_huge = true;
    base::TimeDelta idle_min_time = base::Hours(20);
    base::TimeDelta idle_max_time = base::Hours(25);

    friend std::ostream& operator<<(std::ostream& out,
                                    const ZramWritebackParams& p) {
      out << "[";
      out << "backing_dev_size_mib=" << p.backing_dev_size_mib << " ";
      out << "periodic_time=" << p.periodic_time << " ";
      out << "backoff_time=" << p.backoff_time << " ";
      out << "min_pages=" << p.min_pages << " ";
      out << "max_pages=" << p.max_pages << " ";
      out << "max_pages_per_day=" << p.max_pages_per_day << " ";
      out << "writeback_huge_idle=" << p.writeback_huge_idle << " ";
      out << "writeback_idle=" << p.writeback_idle << " ";
      out << "writeback_huge=" << p.writeback_huge << " ";
      out << "idle_min_time=" << p.idle_min_time << " ";
      out << "idle_max_time=" << p.idle_max_time << " ";
      out << "]";

      return out;
    }
  } params_;

  // For tracking writeback daily limit.
  std::list<std::pair<base::TimeTicks, uint64_t>> history_;
  uint64_t GetWritebackDailyLimit();
  void AddRecord(uint64_t wb_pages);

  uint64_t wb_size_bytes_ = 0;
  uint64_t wb_nr_blocks_ = 0;
  uint64_t stateful_block_size_ = 0;

  uint64_t zram_nr_pages_ = 0;
  bool is_currently_writing_back_ = false;
  base::Time last_writeback_ = base::Time::Min();

  base::WeakPtrFactory<ZramWriteback> weak_factory_{this};
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_ZRAM_WRITEBACK_H_
