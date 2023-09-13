// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_METADATA_MOCK_METADATA_H_
#define DLCSERVICE_METADATA_MOCK_METADATA_H_

#include <optional>
#include <set>

#include <base/values.h>
#include <gmock/gmock.h>

#include "dlcservice/metadata/metadata_interface.h"

namespace dlcservice::metadata {

class MockMetadata : public MetadataInterface {
 public:
  MockMetadata() {
    ON_CALL(*this, GetCache).WillByDefault(testing::ReturnRef(cache_));
    ON_CALL(*this, GetFileIds).WillByDefault(testing::ReturnRef(file_ids_));
  }
  ~MockMetadata() override = default;

  MockMetadata(const MockMetadata&) = delete;
  MockMetadata& operator=(const MockMetadata&) = delete;

  MOCK_METHOD(bool, Initialize, (), (override));
  MOCK_METHOD(std::optional<Entry>, Get, (const DlcId&), (override));
  MOCK_METHOD(bool, Set, (const DlcId&, const Entry&), (override));
  MOCK_METHOD(bool, LoadMetadata, (const DlcId&), (override));
  MOCK_METHOD(void, UpdateFileIds, (), (override));
  MOCK_METHOD(const base::Value::Dict&, GetCache, (), (const override));
  MOCK_METHOD(const std::set<DlcId>&, GetFileIds, (), (const override));

 private:
  base::Value::Dict cache_;
  std::set<DlcId> file_ids_;
};

}  // namespace dlcservice::metadata

#endif  // DLCSERVICE_METADATA_MOCK_METADATA_H_
