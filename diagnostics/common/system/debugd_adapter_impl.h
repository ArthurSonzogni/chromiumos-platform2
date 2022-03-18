// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_IMPL_H_
#define DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_IMPL_H_

#include <memory>

#include "diagnostics/common/system/debugd_adapter.h"

namespace org {
namespace chromium {
class debugdProxyInterface;
}  // namespace chromium
}  // namespace org

namespace diagnostics {

class DebugdAdapterImpl final : public DebugdAdapter {
 public:
  explicit DebugdAdapterImpl(
      std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy);
  DebugdAdapterImpl(const DebugdAdapterImpl&) = delete;
  DebugdAdapterImpl& operator=(const DebugdAdapterImpl&) = delete;
  ~DebugdAdapterImpl() override;

  // DebugdAdapter overrides:
  void GetSmartAttributes(OnceStringResultCallback callback) override;
  void GetNvmeIdentity(OnceStringResultCallback callback) override;
  StringResult GetNvmeIdentitySync() override;
  void RunNvmeShortSelfTest(OnceStringResultCallback callback) override;
  void RunNvmeLongSelfTest(OnceStringResultCallback callback) override;
  void StopNvmeSelfTest(OnceStringResultCallback callback) override;
  void GetNvmeLog(uint32_t page_id,
                  uint32_t length,
                  bool raw_binary,
                  OnceStringResultCallback callback) override;

 private:
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_DEBUGD_ADAPTER_IMPL_H_
