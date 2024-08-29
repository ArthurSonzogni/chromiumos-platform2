// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/blob_util.h"

#include <base/check.h>
#include <base/logging.h>
#include <google/protobuf/message_lite.h>

namespace login_manager {

std::vector<uint8_t> SerializeAsBlob(
    const google::protobuf::MessageLite& message) {
  std::vector<uint8_t> result;
  result.resize(message.ByteSizeLong());
  CHECK(message.SerializeToArray(result.data(), result.size()))
      << "Failed to serialize protobuf.";
  return result;
}

std::vector<uint8_t> StringToBlob(std::string_view str) {
  return std::vector<uint8_t>(str.begin(), str.end());
}

std::string BlobToString(const std::vector<uint8_t>& blob) {
  return std::string(reinterpret_cast<const char*>(blob.data()), blob.size());
}

}  // namespace login_manager
