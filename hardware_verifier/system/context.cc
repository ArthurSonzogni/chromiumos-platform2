// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/system/context.h"

#include <base/check_op.h>
#include <base/files/file_path.h>

namespace hardware_verifier {
namespace {
Context* g_instance = nullptr;

const base::FilePath root_dir_{"/"};
}  // namespace

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

const base::FilePath& Context::root_dir() {
  return root_dir_;
}

}  // namespace hardware_verifier
