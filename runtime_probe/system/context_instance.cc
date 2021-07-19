// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/system/context_instance.h"

#include <base/check.h>

namespace runtime_probe {
namespace {
Context* g_context = nullptr;
}  // namespace

Context* ContextInstance::Get() {
  CHECK(g_context) << "Context instance has not yet been set.";
  return g_context;
}

void ContextInstance::Set(Context* context) {
  CHECK(context) << "Expected context object but got nullptr";
  CHECK(!g_context) << "Context instance has already been set.";
  g_context = context;
}

}  // namespace runtime_probe
