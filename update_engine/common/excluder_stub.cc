// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/excluder_stub.h"

#include <memory>

#include "update_engine/common/prefs_interface.h"

using std::string;

namespace chromeos_update_engine {

std::unique_ptr<ExcluderInterface> CreateExcluder() {
  return std::make_unique<ExcluderStub>();
}

bool ExcluderStub::Exclude(const string& name) {
  return true;
}

bool ExcluderStub::IsExcluded(const string& name) {
  return false;
}

bool ExcluderStub::Reset() {
  return true;
}

}  // namespace chromeos_update_engine
