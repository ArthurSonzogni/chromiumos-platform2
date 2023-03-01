// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_SWAP_TOOL_H_
#define SWAP_MANAGEMENT_SWAP_TOOL_H_

#include <string>
#include <vector>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/files/file_path.h>
#include <brillo/errors/error.h>

namespace swap_management {

class SwapTool {
 public:
  SwapTool() = default;
  SwapTool(const SwapTool&) = delete;
  SwapTool& operator=(const SwapTool&) = delete;

  virtual ~SwapTool() = default;

  absl::Status SwapStart();
  absl::Status SwapStop();
  absl::Status SwapSetSize(uint32_t size);
  std::string SwapStatus();

  // Zram writeback configuration.
  std::string SwapZramEnableWriteback(uint32_t size_mb) const;
  std::string SwapZramSetWritebackLimit(uint32_t num_pages) const;
  std::string SwapZramMarkIdle(uint32_t age_seconds) const;
  std::string InitiateSwapZramWriteback(uint32_t mode) const;

  // MGLRU configuration.
  bool MGLRUSetEnable(brillo::ErrorPtr* error, bool enable) const;

  // virtual and protected for testing
 protected:
  virtual absl::Status RunProcessHelper(
      const std::vector<std::string>& commands);
  virtual absl::Status WriteFile(const base::FilePath& path,
                                 const std::string& data);
  virtual absl::Status ReadFileToStringWithMaxSize(const base::FilePath& path,
                                                   std::string* contents,
                                                   size_t max_size);
  virtual absl::Status ReadFileToString(const base::FilePath& path,
                                        std::string* contents);
  virtual absl::Status DeleteFile(const base::FilePath& path);

 private:
  absl::StatusOr<bool> IsZramSwapOn();
  absl::StatusOr<uint64_t> GetMemTotal();
  absl::Status SetDefaultLowMemoryMargin(uint64_t mem_total);
  absl::Status InitializeMMTunables(uint64_t mem_total);
  absl::StatusOr<uint64_t> GetZramSize(uint64_t mem_total);
  absl::Status EnableZramSwapping();
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_SWAP_TOOL_H_
