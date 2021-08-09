// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/crossystem/crossystem.h>

#include <base/optional.h>
#include <vboot/crossystem.h>

namespace brillo {

base::Optional<int> CrossystemImpl::VbGetSystemPropertyInt(
    const std::string& name) const {
  int value = ::VbGetSystemPropertyInt(name.c_str());
  if (value == -1)
    return base::nullopt;
  return value;
}

bool CrossystemImpl::VbSetSystemPropertyInt(const std::string& name,
                                            int value) {
  return 0 == ::VbSetSystemPropertyInt(name.c_str(), value);
}

base::Optional<std::string> CrossystemImpl::VbGetSystemPropertyString(
    const std::string& name) const {
  char value_buffer[VB_MAX_STRING_PROPERTY];
  if (NULL == ::VbGetSystemPropertyString(name.c_str(), value_buffer,
                                          sizeof(value_buffer)))
    return base::nullopt;
  return value_buffer;
}

bool CrossystemImpl::VbSetSystemPropertyString(const std::string& name,
                                               const std::string& value) {
  return 0 == ::VbSetSystemPropertyString(name.c_str(), value.c_str());
}

}  // namespace brillo
