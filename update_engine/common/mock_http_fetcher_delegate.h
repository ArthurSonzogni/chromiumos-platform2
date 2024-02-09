// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>

#include "update_engine/common/http_fetcher.h"

namespace chromeos_update_engine {

// Mock interface for delegates
class MockHttpFetcherDelegate : public HttpFetcherDelegate {
 public:
  MockHttpFetcherDelegate() = default;
  ~MockHttpFetcherDelegate() = default;

  MockHttpFetcherDelegate(const MockHttpFetcherDelegate&) = delete;
  MockHttpFetcherDelegate& operator=(const MockHttpFetcherDelegate&) = delete;

  MOCK_METHOD(bool,
              ReceivedBytes,
              (HttpFetcher*, const void*, size_t),
              (override));
  MOCK_METHOD(void, SeekToOffset, (off_t), (override));
  MOCK_METHOD(void, TransferComplete, (HttpFetcher*, bool), (override));
  MOCK_METHOD(void, TransferTerminated, (HttpFetcher*), (override));
};

}  // namespace chromeos_update_engine
