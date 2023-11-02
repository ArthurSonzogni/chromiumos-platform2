//
// Copyright 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

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
