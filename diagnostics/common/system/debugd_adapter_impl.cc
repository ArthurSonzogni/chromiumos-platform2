// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/system/debugd_adapter_impl.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <brillo/errors/error.h>

#include "debugd/dbus-proxies.h"

namespace diagnostics {

namespace {

constexpr char kNvmeIdentityOption[] = "identify_controller";

}  // namespace

DebugdAdapterImpl::DebugdAdapterImpl(
    std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy)
    : debugd_proxy_(std::move(debugd_proxy)) {
  DCHECK(debugd_proxy_);
}

DebugdAdapterImpl::~DebugdAdapterImpl() = default;

DebugdAdapter::StringResult DebugdAdapterImpl::GetNvmeIdentitySync() {
  StringResult result;
  debugd_proxy_->Nvme(kNvmeIdentityOption, &result.value, &result.error);
  return result;
}

}  // namespace diagnostics
