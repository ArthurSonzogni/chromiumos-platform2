// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>

#include "runtime_probe/system/context_runtime_impl.h"

namespace runtime_probe {
ContextRuntimeImpl::ContextRuntimeImpl() {
  CHECK(SetupDBusServices()) << "Cannot setup dbus service";
}

}  // namespace runtime_probe
