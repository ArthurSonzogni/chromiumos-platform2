// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_MOCK_HTTP_SENDER_H_
#define FLEX_HWIS_MOCK_HTTP_SENDER_H_

#include "flex_hwis/http_sender.h"

#include <gmock/gmock.h>

namespace flex_hwis {
class MockHttpSender : public HttpSender {
 public:
  MockHttpSender() = default;
  MockHttpSender(const MockHttpSender&) = delete;
  MockHttpSender& operator=(const MockHttpSender&) = delete;

  MOCK_METHOD(bool,
              DeleteDevice,
              (const hwis_proto::Device& content),
              (override));
  MOCK_METHOD(bool,
              UpdateDevice,
              (const hwis_proto::Device& content),
              (override));
  MOCK_METHOD(PostActionResponse,
              RegisterNewDevice,
              (const hwis_proto::Device& content),
              (override));
};
}  // namespace flex_hwis

#endif  // FLEX_HWIS_MOCK_HTTP_SENDER_H_
