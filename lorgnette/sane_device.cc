// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/constants.h"
#include "lorgnette/sane_device.h"
#include "lorgnette/uuid_util.h"

namespace lorgnette {

std::vector<std::string> SaneDevice::GetSupportedFormats() const {
  // TODO(bmgordon): When device pass-through is available, add a hook for
  // subclasses to add additional formats.
  return {kJpegMimeType, kPngMimeType};
}

std::optional<std::string> SaneDevice::GetCurrentJob() const {
  return current_job_;
}

void SaneDevice::StartJob() {
  current_job_ = GenerateUUID();
}

void SaneDevice::EndJob() {
  current_job_.reset();
}

}  // namespace lorgnette
