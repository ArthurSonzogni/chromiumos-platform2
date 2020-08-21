// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_MOCK_EXAMPLE_DATABASE_H_
#define FEDERATED_MOCK_EXAMPLE_DATABASE_H_

#include <string>

#include <gmock/gmock.h>

#include "federated/example_database.h"

namespace federated {

class MockExampleDatabase : public ExampleDatabase {
 public:
  explicit MockExampleDatabase(const base::FilePath& db_path);
  MockExampleDatabase(const MockExampleDatabase&) = delete;
  MockExampleDatabase& operator=(const MockExampleDatabase&) = delete;

  ~MockExampleDatabase() override = default;

  MOCK_METHOD(bool, Init, (), (override));
  MOCK_METHOD(bool, IsOpen, (), (const, override));
  MOCK_METHOD(bool, Close, (), (override));
  MOCK_METHOD(bool, CheckIntegrity, (), (const, override));
  MOCK_METHOD(bool, InsertExample, (const ExampleRecord&), (override));
  MOCK_METHOD(bool,
              PrepareStreamingForClient,
              (const std::string&, const int32_t),
              (override));
  MOCK_METHOD(base::Optional<ExampleRecord>,
              GetNextStreamedRecord,
              (),
              (override));
  MOCK_METHOD(void, CloseStreaming, (), (override));
  MOCK_METHOD(bool,
              DeleteExamplesWithSmallerIdForClient,
              (const std::string&, const int64_t),
              (override));
};

}  // namespace federated

#endif  // FEDERATED_MOCK_EXAMPLE_DATABASE_H_
