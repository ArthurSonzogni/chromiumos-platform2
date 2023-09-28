// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_SWAP_TOOL_H_
#define SWAP_MANAGEMENT_SWAP_TOOL_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/files/file_path.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>

#include "featured/feature_library.h"

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

class SwapTool {
 public:
  SwapTool() = default;
  explicit SwapTool(feature::PlatformFeatures*);
  SwapTool(const SwapTool&) = delete;
  SwapTool& operator=(const SwapTool&) = delete;

  ~SwapTool() = default;

  absl::Status SwapStart();
  absl::Status SwapStop();
  absl::Status SwapSetSize(int32_t size);
  absl::Status SwapSetSwappiness(uint32_t swappiness);
  std::string SwapStatus();

  // Zram writeback configuration.
  absl::Status SwapZramEnableWriteback(uint32_t size_mb);
  absl::Status SwapZramSetWritebackLimit(uint32_t num_pages);
  absl::Status SwapZramMarkIdle(uint32_t age_seconds);
  absl::Status InitiateSwapZramWriteback(ZramWritebackMode mode);

  // MGLRU configuration.
  absl::Status MGLRUSetEnable(uint8_t value);

  // Zram Recompression
  absl::Status SwapZramSetRecompAlgorithms(
      const std::vector<std::string>& algos);
  absl::Status InitiateSwapZramRecompression(ZramRecompressionMode mode,
                                             uint32_t threshold,
                                             const std::string& algo);

 private:
  absl::StatusOr<bool> IsZramSwapOn();
  absl::StatusOr<uint64_t> GetMemTotalKiB();
  absl::StatusOr<uint64_t> GetUserConfigZramSizeBytes();
  absl::StatusOr<uint64_t> GetZramSizeBytes();
  void SetRecompAlgorithms();
  void SetCompAlgorithmIfOverriden();
  absl::Status EnableZramSwapping();
  std::optional<std::string> GetFeatureParam(const VariationsFeature& vf,
                                             const std::string& key);

  uint64_t wb_size_bytes_ = 0;
  uint64_t wb_nr_blocks_ = 0;
  uint64_t stateful_block_size_ = 0;

  feature::PlatformFeatures* platform_features_ = nullptr;

  void CleanupWriteback();
  absl::Status ZramWritebackPrerequisiteCheck(uint32_t size);
  absl::Status GetZramWritebackInfo(uint32_t size);
  absl::Status CreateDmDevicesAndEnableWriteback();
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_SWAP_TOOL_H_
