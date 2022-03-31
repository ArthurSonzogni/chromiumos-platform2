// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/real_tpm_handle_manager.h"

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <trunks/mock_response_serializer.h>
#include <trunks/tpm_generated.h>

#include "vtpm/backends/fake_blob.h"

namespace vtpm {

namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Return;
using ::testing::StrictMock;

constexpr trunks::TPM_HANDLE kFakeHandle1 = trunks::PERSISTENT_FIRST + 10;
constexpr trunks::TPM_HANDLE kFakeHandle2 = trunks::PERSISTENT_FIRST + 100;
constexpr trunks::TPM_HANDLE kFakeHandle3 = trunks::PERSISTENT_FIRST + 1000;
constexpr char kFakeBlob1[] = "blob1";
constexpr char kFakeBlob2[] = "blob2";
constexpr char kFakeBlob3[] = "blob3";

static_assert(kFakeHandle1 < kFakeHandle2, "");
static_assert(kFakeHandle2 < kFakeHandle3, "");

}  // namespace

class RealTpmHandleManagerTest : public testing::Test {
 public:
  void SetUp() override {
    std::map<trunks::TPM_HANDLE, Blob*> table{
        {kFakeHandle1, &mock_blob_1_},
        {kFakeHandle2, &mock_blob_2_},
        {kFakeHandle3, &mock_blob_3_},
    };
    manager_ = std::make_unique<RealTpmHandleManager>(table);
  }

 protected:
  StrictMock<FakeBlob> mock_blob_1_{kFakeBlob1};
  StrictMock<FakeBlob> mock_blob_2_{kFakeBlob2};
  StrictMock<FakeBlob> mock_blob_3_{kFakeBlob3};
  std::unique_ptr<RealTpmHandleManager> manager_;
};

namespace {

TEST_F(RealTpmHandleManagerTest, IsHandleTypeSuppoerted) {
  EXPECT_TRUE(manager_->IsHandleTypeSuppoerted(trunks::HR_PERSISTENT));
  EXPECT_TRUE(manager_->IsHandleTypeSuppoerted(trunks::HR_PERSISTENT + 1));
  EXPECT_FALSE(manager_->IsHandleTypeSuppoerted(trunks::HR_PERMANENT));
}

TEST_F(RealTpmHandleManagerTest, GetHandleList) {
  EXPECT_CALL(mock_blob_1_, Get(_));
  EXPECT_CALL(mock_blob_2_, Get(_));
  EXPECT_CALL(mock_blob_3_, Get(_));
  std::vector<trunks::TPM_HANDLE> found_handles;
  EXPECT_EQ(manager_->GetHandleList(0, &found_handles), trunks::TPM_RC_SUCCESS);
  EXPECT_THAT(found_handles,
              ElementsAre(kFakeHandle1, kFakeHandle2, kFakeHandle3));
}

TEST_F(RealTpmHandleManagerTest, GetHandleListSkipFirst) {
  EXPECT_CALL(mock_blob_2_, Get(_));
  EXPECT_CALL(mock_blob_3_, Get(_));
  std::vector<trunks::TPM_HANDLE> found_handles;
  EXPECT_EQ(manager_->GetHandleList(kFakeHandle1 + 1, &found_handles),
            trunks::TPM_RC_SUCCESS);
  EXPECT_THAT(found_handles, ElementsAre(kFakeHandle2, kFakeHandle3));
}

TEST_F(RealTpmHandleManagerTest, GetHandleListEmpty) {
  std::vector<trunks::TPM_HANDLE> found_handles;
  EXPECT_EQ(manager_->GetHandleList(kFakeHandle3 + 1, &found_handles),
            trunks::TPM_RC_SUCCESS);
  EXPECT_TRUE(found_handles.empty());
}

TEST_F(RealTpmHandleManagerTest, GetHandleListError) {
  EXPECT_CALL(mock_blob_1_, Get(_));
  EXPECT_CALL(mock_blob_2_, Get(_)).WillOnce(Return(trunks::TPM_RC_FAILURE));
  std::vector<trunks::TPM_HANDLE> found_handles;
  EXPECT_EQ(manager_->GetHandleList(0, &found_handles), trunks::TPM_RC_FAILURE);
}

}  // namespace

}  // namespace vtpm
