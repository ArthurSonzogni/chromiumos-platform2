// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libcrossystem/crossystem.h>

#include <optional>

#include <vboot/crossystem.h>

namespace crossystem {

std::optional<int> CrossystemImpl::VbGetSystemPropertyInt(
    const std::string& name) const {
  int value = ::VbGetSystemPropertyInt(name.c_str());
  if (value == -1)
    return std::nullopt;
  return value;
}

bool CrossystemImpl::VbSetSystemPropertyInt(const std::string& name,
                                            int value) {
  return 0 == ::VbSetSystemPropertyInt(name.c_str(), value);
}

std::optional<std::string> CrossystemImpl::VbGetSystemPropertyString(
    const std::string& name) const {
  char value_buffer[VB_MAX_STRING_PROPERTY];
  if (NULL == ::VbGetSystemPropertyString(name.c_str(), value_buffer,
                                          sizeof(value_buffer)))
    return std::nullopt;
  return value_buffer;
}

bool CrossystemImpl::VbSetSystemPropertyString(const std::string& name,
                                               const std::string& value) {
  return 0 == ::VbSetSystemPropertyString(name.c_str(), value.c_str());
}

}  // namespace crossystem
