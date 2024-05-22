// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/error/tpm_retry_handler.h"

#include <utility>

#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>

#include "libhwsec/error/tpm_error.h"
#include "libhwsec/middleware/function_name.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"

namespace {
constexpr int kMaxTryCount = 5;
constexpr base::TimeDelta kInitialRetry = base::Milliseconds(100);
constexpr double kRetryMultiplier = 2.0;
}  // namespace

namespace hwsec {

TPMRetryHandler::TPMRetryHandler()
    : remaining_try_count_(kMaxTryCount), current_delay_(kInitialRetry) {}

void TPMRetryHandler::DelayAndUpdate() {
#if !USE_FUZZER
  base::PlatformThread::Sleep(current_delay_);
#endif
  current_delay_ *= kRetryMultiplier;
  remaining_try_count_--;
}

template <>
bool TPMRetryHandler::ReloadObject(hwsec::Backend& backend,
                                   Metrics* metrics,
                                   const Key& key) {
  auto* key_mgr = backend.Get<Backend::KeyManagement>();
  if (key_mgr == nullptr) {
    return false;
  }

  Status status = key_mgr->ReloadIfPossible(key);
  if (metrics) {
    metrics->SendFuncResultToUMA(
        SimplifyFuncName(GetFuncName<&KeyManagement::ReloadIfPossible>()),
        status);
  }
  if (!status.ok()) {
    LOG(WARNING) << "Failed to reload key parameter: " << status.status();
    return false;
  }
  return true;
}

bool TPMRetryHandler::FlushInvalidSessions(hwsec::Backend& backend,
                                           Metrics* metrics) {
  auto* session_mgr = backend.Get<Backend::SessionManagement>();
  if (session_mgr == nullptr) {
    return false;
  }

  Status status = session_mgr->FlushInvalidSessions();
  if (metrics) {
    metrics->SendFuncResultToUMA(
        SimplifyFuncName(
            GetFuncName<&SessionManagement::FlushInvalidSessions>()),
        status);
  }
  if (!status.ok()) {
    LOG(WARNING) << "Failed to flush invalid sessions: " << status.status();
    return false;
  }
  return true;
}

bool TPMRetryHandler::SyncPinWeaverHashTree(hwsec::Backend& backend,
                                            Metrics* metrics) {
  auto* pinweaver_manager = backend.Get<Backend::PinWeaverManager>();
  if (pinweaver_manager == nullptr) {
    return false;
  }

  Status status = pinweaver_manager->SyncHashTree();
  if (metrics) {
    metrics->SendFuncResultToUMA(
        SimplifyFuncName(GetFuncName<&PinWeaverManager::SyncHashTree>()),
        status);
  }
  if (!status.ok()) {
    LOG(WARNING) << "Failed to sync pinweaver hash tree: "
                 << status.err_status();
    return false;
  }
  return true;
}

}  // namespace hwsec
