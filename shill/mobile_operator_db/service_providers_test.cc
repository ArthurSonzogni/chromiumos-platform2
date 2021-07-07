// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <google/protobuf/repeated_field.h>
#include <gtest/gtest.h>

#include "shill/mobile_operator_db/mobile_operator_db.pb.h"
#include "shill/protobuf_lite_streams.h"

using testing::Test;

namespace shill {

class ServiceProvidersTest : public testing::Test {
 protected:
  // Per-test-suite set-up.
  // Called before the first test in this test suite.
  static void SetUpTestSuite() {
    const char* out_dir = getenv("OUT");
    CHECK_NE(out_dir, nullptr);
    base::FilePath database_path =
        base::FilePath(out_dir).Append("serviceproviders.pbf");
    const char* database_path_cstr = database_path.value().c_str();
    std::unique_ptr<google::protobuf::io::CopyingInputStreamAdaptor>
        database_stream;
    database_stream.reset(protobuf_lite_file_input_stream(database_path_cstr));
    ASSERT_NE(database_stream, nullptr);
    database_ = std::make_unique<shill::mobile_operator_db::MobileOperatorDB>();
    ASSERT_TRUE(database_->ParseFromZeroCopyStream(database_stream.get()));
  }
  // Per-test-suite tear-down.
  // Called after the last test in this test suite.
  static void TearDownTestSuite() {
    database_.reset();
    database_ = nullptr;
  }

  void ValidateUuid(const shill::mobile_operator_db::Data& data,
                    std::set<std::string>* uuids) {
    ASSERT_TRUE(data.has_uuid());
    EXPECT_EQ(uuids->count(data.uuid()), 0)
        << "Non unique uuid: " << data.uuid();
    uuids->emplace(data.uuid());
  }

  // Expensive resource shared by all tests.
  static std::unique_ptr<mobile_operator_db::MobileOperatorDB> database_;
};

std::unique_ptr<mobile_operator_db::MobileOperatorDB>
    ServiceProvidersTest::database_ = nullptr;

TEST_F(ServiceProvidersTest, CheckUniqueUUIDs) {
  // Verify that we are not using the same uuid for different MNOs/MVNOs.
  // This is a common mistake when copy/pasting carrier info.
  std::set<std::string> uuids;
  for (const auto& mno : database_->mno()) {
    ValidateUuid(mno.data(), &uuids);
    for (const auto& mvno : mno.mvno()) {
      ValidateUuid(mvno.data(), &uuids);
    }
  }
  for (const auto& mvno : database_->mvno()) {
    ValidateUuid(mvno.data(), &uuids);
  }
}

TEST_F(ServiceProvidersTest, CheckRootLevelMvnosWithoutFilters) {
  // If a MVNO is at the root level(not under an MNO), and there is no filter
  // in it, the MVNO will always be selected.
  for (const auto& mvno : database_->mvno()) {
    EXPECT_TRUE(mvno.mvno_filter_size() > 0)
        << "MVNO with uuid: " << mvno.data().uuid()
        << " does not have a filter.";
  }
}

}  // namespace shill
