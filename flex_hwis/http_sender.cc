// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/http_sender.h"

#include <string>

#include <base/logging.h>
#include <brillo/http/http_utils.h>
#include <brillo/mime_utils.h>

namespace flex_hwis {

HttpSender::HttpSender(const std::string server_url)
    : server_url_(server_url) {}

bool HttpSender::DeleteDevice(const hwis_proto::Device& content) {
  // TODO(tinghaolin): Implement real logic to handle DELETE API.
  return true;
}

bool HttpSender::UpdateDevice(const hwis_proto::Device& content) {
  // TODO(tinghaolin): Implement real logic to handle PUT API.
  return true;
}

PostActionResponse HttpSender::RegisterNewDevice(
    const hwis_proto::Device& content) {
  // TODO(tinghaolin): Implement real logic to handle POST API.
  PostActionResponse response(true, "");
  return response;
}

}  // namespace flex_hwis
