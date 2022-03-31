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
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory_for_test.h>

#include <base/logging.h>

#include "vtpm/backends/fake_blob.h"
#include "vtpm/backends/scoped_host_key_handle.h"

namespace vtpm {

namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::UnorderedElementsAreArray;

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
    manager_ = std::make_unique<RealTpmHandleManager>(&trunks_factory_, table);

    trunks_factory_.set_tpm_utility(&mock_tpm_utility_);
    trunks_factory_.set_tpm(&mock_tpm_);
    SetDefaultLoadFlushBehavior();
  }
  void TearDown() override {
    // Make sure no memory leak in any case.
    EXPECT_EQ(flushed_host_handles_.size(), loaded_host_handles_.size());
    EXPECT_THAT(flushed_host_handles_,
                UnorderedElementsAreArray(loaded_host_handles_));
  }

 protected:
  trunks::TPM_RC FakeLoadKey(const std::string& /*key_blob*/,
                             trunks::AuthorizationDelegate* /*delegate*/,
                             trunks::TPM_HANDLE* key_handle) {
    *key_handle = trunks::TRANSIENT_FIRST + loaded_host_handles_.size();
    loaded_host_handles_.push_back(*key_handle);
    return trunks::TPM_RC_SUCCESS;
  }
  trunks::TPM_RC FakeFlushKey(
      trunks::TPM_HANDLE key_handle,
      trunks::AuthorizationDelegate* /*authorization_delegate*/) {
    flushed_host_handles_.push_back(key_handle);
    return trunks::TPM_RC_SUCCESS;
  }
  void SetDefaultLoadFlushBehavior() {
    ON_CALL(mock_tpm_utility_, LoadKey(_, _, _))
        .WillByDefault(Invoke(this, &RealTpmHandleManagerTest::FakeLoadKey));
    ON_CALL(mock_tpm_, FlushContextSync(_, _))
        .WillByDefault(Invoke(this, &RealTpmHandleManagerTest::FakeFlushKey));
  }

  std::vector<trunks::TPM_HANDLE> loaded_host_handles_;
  std::vector<trunks::TPM_HANDLE> flushed_host_handles_;

  StrictMock<FakeBlob> mock_blob_1_{kFakeBlob1};
  StrictMock<FakeBlob> mock_blob_2_{kFakeBlob2};
  StrictMock<FakeBlob> mock_blob_3_{kFakeBlob3};
  trunks::TrunksFactoryForTest trunks_factory_;
  trunks::MockTpmUtility mock_tpm_utility_;
  trunks::MockTpm mock_tpm_;
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

TEST_F(RealTpmHandleManagerTest, TranslateHandleSuccess) {
  EXPECT_CALL(mock_blob_1_, Get(_));
  ScopedHostKeyHandle host_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(kFakeBlob1, _, _));
  EXPECT_EQ(manager_->TranslateHandle(kFakeHandle1, &host_handle),
            trunks::TPM_RC_SUCCESS);
  // NOTE that we don't validate the exact value of the returned handle because
  // it's up to implementation of the mocks.
  EXPECT_NE(host_handle.Get(), trunks::TPM_HANDLE());
  EXPECT_CALL(mock_tpm_, FlushContextSync(host_handle.Get(), _));
}

TEST_F(RealTpmHandleManagerTest, TranslateHandleSuccessMovedScopedHostHandle) {
  EXPECT_CALL(mock_blob_1_, Get(_));
  ScopedHostKeyHandle host_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(kFakeBlob1, _, _));
  EXPECT_EQ(manager_->TranslateHandle(kFakeHandle1, &host_handle),
            trunks::TPM_RC_SUCCESS);
  // NOTE that we don't validate the exact value of the returned handle because
  // it's up to implementation of the mocks.
  EXPECT_NE(host_handle.Get(), trunks::TPM_HANDLE());
  EXPECT_CALL(mock_tpm_, FlushContextSync(host_handle.Get(), _));
  ScopedHostKeyHandle moved_host_handle = std::move(host_handle);
}

}  // namespace

}  // namespace vtpm
