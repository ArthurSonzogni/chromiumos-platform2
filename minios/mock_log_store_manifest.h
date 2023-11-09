// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MOCK_LOG_STORE_MANIFEST_H_
#define MINIOS_MOCK_LOG_STORE_MANIFEST_H_

#include <gmock/gmock.h>

#include "minios/log_store_manifest_interface.h"

namespace minios {

class MockLogStoreManifest : public LogStoreManifestInterface {
 public:
  MockLogStoreManifest() = default;

  MOCK_METHOD(bool, Generate, (const LogManifest::Entry& entry), (override));
  MOCK_METHOD(std::optional<LogManifest>, Retreive, (), (override));
  MOCK_METHOD(bool, Write, (), (override));
  MOCK_METHOD(void, Clear, (), (override));
};

}  // namespace minios

#endif  // MINIOS_MOCK_LOG_STORE_MANIFEST_H_
