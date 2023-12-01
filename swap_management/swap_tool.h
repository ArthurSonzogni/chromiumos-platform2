// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_SWAP_TOOL_H_
#define SWAP_MANAGEMENT_SWAP_TOOL_H_

#include "featured/feature_library.h"

#include <cstdint>
#include <map>
#include <string>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/files/file_path.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>

namespace swap_management {

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

  // Zram writeback configuration, used by writeback logic in Chromium.
  // TODO(ctshao): Cleanup once the finch experiment is done: cl/459290244
  absl::Status SwapZramEnableWriteback(uint32_t size_mb);
  absl::Status SwapZramSetWritebackLimit(uint32_t num_pages);
  absl::Status SwapZramMarkIdle(uint32_t age_seconds);
  absl::Status InitiateSwapZramWriteback(ZramWritebackMode mode);

  // MGLRU configuration.
  absl::Status MGLRUSetEnable(uint8_t value);

 private:
  absl::StatusOr<bool> IsZramSwapOn();
  absl::StatusOr<uint64_t> GetMemTotalKiB();
  absl::StatusOr<uint64_t> GetUserConfigZramSizeBytes();
  absl::StatusOr<uint64_t> GetZramSizeBytes();
  void SetCompAlgorithmIfOverridden();
  absl::Status EnableZramSwapping();
  std::optional<std::map<std::string, std::string>> GetFeatureParams(
      const VariationsFeature& vf);
  std::optional<std::string> GetFeatureParamValue(const VariationsFeature& vf,
                                                  const std::string& key);
  absl::Status EnableZramRecompression();
  absl::Status EnableZramWriteback();

  feature::PlatformFeatures* platform_features_ = nullptr;
  bool zram_recompression_configured_ = false;
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_SWAP_TOOL_H_
