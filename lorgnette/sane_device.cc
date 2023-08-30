// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/constants.h"
#include "lorgnette/sane_device.h"

namespace lorgnette {

std::vector<std::string> SaneDevice::GetSupportedFormats() const {
  // TODO(bmgordon): When device pass-through is available, add a hook for
  // subclasses to add additional formats.
  return {kJpegMimeType, kPngMimeType};
}

}  // namespace lorgnette
