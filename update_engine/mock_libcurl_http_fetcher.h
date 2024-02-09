// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_MOCK_LIBCURL_HTTP_FETCHER_H_
#define UPDATE_ENGINE_MOCK_LIBCURL_HTTP_FETCHER_H_

#include <gmock/gmock.h>

#include "update_engine/libcurl_http_fetcher.h"

namespace chromeos_update_engine {

class MockLibcurlHttpFetcher : public LibcurlHttpFetcher {
 public:
  MockLibcurlHttpFetcher(ProxyResolver* proxy_resolver,
                         HardwareInterface* hardware)
      : LibcurlHttpFetcher(proxy_resolver, hardware) {}

  MOCK_METHOD(void, GetHttpResponseCode, (), (override));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_MOCK_LIBCURL_HTTP_FETCHER_H_
