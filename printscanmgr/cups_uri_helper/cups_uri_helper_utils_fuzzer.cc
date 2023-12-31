// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "printscanmgr/cups_uri_helper/cups_uri_helper_utils.h"

namespace printscanmgr {
namespace cups_helper {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  UriSeemsReasonable(std::string(data, data + size));

  return 0;
}

}  // namespace cups_helper
}  // namespace printscanmgr
