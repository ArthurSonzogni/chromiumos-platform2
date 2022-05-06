// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/vek_cert_manager.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <trunks/tpm_generated.h>

#include "vtpm/backends/fake_blob.h"

namespace vtpm {

namespace {

constexpr char kFakeCert[] = "fake cert";
constexpr trunks::TPM_NV_INDEX kFakeIndex = 0x00806449;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::StrictMock;

}  // namespace

// A placeholder test fixture.
class VekCertManagerTest : public testing::Test {
 protected:
  FakeBlob mock_blob_{kFakeCert};
  VekCertManager manager_{kFakeIndex, &mock_blob_};
};

namespace {

TEST_F(VekCertManagerTest, ReadSuccess) {
  EXPECT_CALL(mock_blob_, Get(_))
      .WillOnce(
          DoAll(SetArgReferee<0>(kFakeCert), Return(trunks::TPM_RC_SUCCESS)));
  std::string data_out;
  EXPECT_EQ(manager_.Read(kFakeIndex, /*password=*/"", data_out),
            trunks::TPM_RC_SUCCESS);
  EXPECT_EQ(data_out, kFakeCert);
}

TEST_F(VekCertManagerTest, FailureReadError) {
  EXPECT_CALL(mock_blob_, Get(_))
      .WillOnce(
          DoAll(SetArgReferee<0>(kFakeCert), Return(trunks::TPM_RC_FAILURE)));
  std::string data_out;
  EXPECT_EQ(manager_.Read(kFakeIndex, /*password=*/"", data_out),
            trunks::TPM_RC_FAILURE);
}

TEST_F(VekCertManagerTest, FailureNonEmptyAuthNotSupported) {
  std::string data_out;
  EXPECT_EQ(manager_.Read(kFakeIndex, "non empty password", data_out),
            trunks::TPM_RC_BAD_AUTH);
}

TEST_F(VekCertManagerTest, FailureWrongIndex) {
  std::string data_out;
  EXPECT_EQ(manager_.Read(kFakeIndex + 1, /*password=*/"", data_out),
            trunks::TPM_RC_NV_SPACE);
}

}  // namespace

}  // namespace vtpm
