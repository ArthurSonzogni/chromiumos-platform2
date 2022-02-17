// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>

#include <base/check_op.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {
namespace {
Context* g_instance = nullptr;
}

// When the derived classes initialized, it will call the parent class's
// constructor, which set the address of the derived classes to the
// |g_instance| to override the virtual functions.
Context::Context() {
  CHECK(!g_instance)
      << "g_instance has already be set. Is a seccond Context created?";
  g_instance = this;
}

Context::~Context() {
  CHECK_EQ(g_instance, this) << "The context is not the same as g_instance.";
  g_instance = nullptr;
}

// static
Context* Context::Get() {
  DCHECK(g_instance) << "g_instance has not been set.";
  return g_instance;
}

}  // namespace runtime_probe
