// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_SWAP_TOOL_H_
#define SWAP_MANAGEMENT_SWAP_TOOL_H_

#include <string>

#include <brillo/errors/error.h>

namespace swap_management {

class SwapTool {
 public:
  SwapTool() = default;
  SwapTool(const SwapTool&) = delete;
  SwapTool& operator=(const SwapTool&) = delete;

  ~SwapTool() = default;

  std::string SwapEnable(int32_t size, bool change_now) const;
  std::string SwapDisable(bool change_now) const;
  std::string SwapStartStop(bool on) const;
  std::string SwapStatus() const;
  std::string SwapSetParameter(const std::string& parameter_name,
                               uint32_t parameter_value) const;

  // Zram writeback configuration.
  std::string SwapZramEnableWriteback(uint32_t size_mb) const;
  std::string SwapZramSetWritebackLimit(uint32_t num_pages) const;
  std::string SwapZramMarkIdle(uint32_t age_seconds) const;
  std::string InitiateSwapZramWriteback(uint32_t mode) const;

  // MGLRU configuration.
  bool MGLRUSetEnable(brillo::ErrorPtr* error, bool enable) const;
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_SWAP_TOOL_H_
