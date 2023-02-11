// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libcrossystem/crossystem.h>

#include <optional>

namespace crossystem {

std::optional<int> Crossystem::VbGetSystemPropertyInt(
    const std::string& name) const {
  return impl_->VbGetSystemPropertyInt(name);
}

bool Crossystem::VbSetSystemPropertyInt(const std::string& name, int value) {
  return impl_->VbSetSystemPropertyInt(name, value);
}

std::optional<std::string> Crossystem::VbGetSystemPropertyString(
    const std::string& name) const {
  return impl_->VbGetSystemPropertyString(name);
}

bool Crossystem::VbSetSystemPropertyString(const std::string& name,
                                           const std::string& value) {
  return impl_->VbSetSystemPropertyString(name, value);
}

}  // namespace crossystem
