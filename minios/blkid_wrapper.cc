// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/blkid_wrapper.h"

#include <optional>

#include <base/logging.h>

namespace minios {

std::optional<std::string> BlkIdWrapper::HandleTagValue(
    const char* tag_value,
    const std::string& tagname,
    const std::string& devname) const {
  if (!tag_value) {
    LOG(INFO) << "Unable to find tag=" << tagname << " for device=" << devname;
    return std::nullopt;
  }
  return tag_value;
}

bool BlkIdWrapper::HandleGetDevice(const blkid_dev& dev) const {
  return dev;
}

bool BlkIdWrapper::FindDevice(const std::string& devname) const {
  return HandleGetDevice(
      ::blkid_get_dev(cache_, devname.c_str(), BLKID_DEV_NORMAL));
}

void BlkIdWrapper::GetCache() {
  ::blkid_get_cache(&cache_, nullptr);
}

std::optional<std::string> BlkIdWrapper::GetTagValue(
    const std::string& tagname, const std::string& devname) const {
  return HandleTagValue(
      ::blkid_get_tag_value(cache_, tagname.c_str(), devname.c_str()), tagname,
      devname);
}

}  // namespace minios
