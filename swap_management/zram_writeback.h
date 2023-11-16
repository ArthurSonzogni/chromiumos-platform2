// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_ZRAM_WRITEBACK_H_
#define SWAP_MANAGEMENT_ZRAM_WRITEBACK_H_

#include "swap_management/utils.h"

#include <memory>
#include <string>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>

namespace swap_management {

class LoopDev {
 public:
  ~LoopDev();

  static absl::StatusOr<std::unique_ptr<LoopDev>> Create(
      const std::string& path);
  static absl::StatusOr<std::unique_ptr<LoopDev>> Create(
      const std::string& path, bool direct_io, uint32_t sector_size);

  std::string GetPath();

 private:
  LoopDev() = delete;
  explicit LoopDev(std::string path) : path_(path) {}
  LoopDev(const LoopDev&) = delete;
  LoopDev& operator=(const LoopDev&) = delete;

  std::string path_;
};

class DmDev {
 public:
  ~DmDev();

  static absl::StatusOr<std::unique_ptr<DmDev>> Create(
      const std::string& name, const std::string& table_fmt);

  std::string GetPath();

 private:
  DmDev() = delete;
  explicit DmDev(std::string name) : name_(name) {}
  DmDev(const DmDev&) = delete;
  DmDev& operator=(const DmDev&) = delete;

  std::string name_;

  absl::Status Wait();
};

class ZramWriteback {
 public:
  // There are only one zram writeback instance in current setup.
  static ZramWriteback* Get();

  // TODO(ctshao): Move to private once the finch experiment is done:
  // cl/459290244
  absl::Status EnableWriteback(uint32_t size_mb);
  absl::Status SetWritebackLimit(uint32_t num_pages);
  absl::Status MarkIdle(uint32_t age_seconds);
  absl::Status InitiateWriteback(ZramWritebackMode mode);

 private:
  ZramWriteback() = default;
  ZramWriteback& operator=(const ZramWriteback&) = delete;
  ZramWriteback(const ZramWriteback&) = delete;

  ~ZramWriteback();

  uint64_t wb_size_bytes_ = 0;
  uint64_t wb_nr_blocks_ = 0;
  uint64_t stateful_block_size_ = 0;

  void Cleanup();
  absl::Status PrerequisiteCheck(uint32_t size);
  absl::Status GetWritebackInfo(uint32_t size);
  absl::Status CreateDmDevicesAndEnableWriteback();
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_ZRAM_WRITEBACK_H_
