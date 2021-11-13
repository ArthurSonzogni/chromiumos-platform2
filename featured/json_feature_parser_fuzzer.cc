// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include <base/check_op.h>

#include "featured/service.h"

namespace featured {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string data_string(data, data + size);

  JsonFeatureParser parser;
  std::string err_str;
  bool success = parser.ParseFileContents(data_string, &err_str);
  CHECK_EQ(success, err_str.empty());

  return 0;
}

}  // namespace featured
