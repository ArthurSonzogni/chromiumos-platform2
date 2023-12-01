// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/lvm/lvmd_proxy_wrapper.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libimageloader/manifest.h>
#include <lvmd/proto_bindings/lvmd.pb.h>
// NOLINTNEXTLINE(build/include_alpha)
#include <lvmd/dbus-proxy-mocks.h>

#include "dlcservice/utils/mock_utils.h"
#include "dlcservice/utils/utils.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace dlcservice {

class LvmdProxyWrapperTest : public testing::Test {
 public:
  LvmdProxyWrapperTest() = default;
  LvmdProxyWrapperTest(const LvmdProxyWrapperTest&) = delete;
  LvmdProxyWrapperTest& operator=(const LvmdProxyWrapperTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());

    mu_ = std::make_unique<StrictMock<MockUtils>>();
    mu_ptr_ = mu_.get();

    mlvmd_ = std::make_unique<StrictMock<LvmdProxyMock>>();
    mlvmd_ptr_ = mlvmd_.get();

    lvmd_ =
        std::make_unique<LvmdProxyWrapper>(std::move(mlvmd_), std::move(mu_));
  }

 protected:
  base::ScopedTempDir scoped_temp_dir_;

  std::unique_ptr<MockUtils> mu_;
  MockUtils* mu_ptr_ = nullptr;

  using LvmdProxyMock = org::chromium::LvmdProxyMock;
  std::unique_ptr<LvmdProxyMock> mlvmd_;
  LvmdProxyMock* mlvmd_ptr_ = nullptr;

  std::unique_ptr<LvmdProxyWrapper> lvmd_;
};

TEST_F(LvmdProxyWrapperTest, CreateLogicalVolumeGidCheck) {
  auto p = scoped_temp_dir_.GetPath();
  lvmd::LogicalVolume lv;
  lv.set_path(p.value());

  EXPECT_CALL(*mlvmd_ptr_, CreateLogicalVolume(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(lv), Return(true)));
  EXPECT_CALL(*mu_ptr_, MakeAbsoluteFilePath(p)).WillOnce(Return(p));
  EXPECT_CALL(*mu_ptr_, WaitForGid(p, 20777)).WillOnce(Return(true));

  lvmd::LogicalVolume lv_arg;
  EXPECT_TRUE(lvmd_->CreateLogicalVolume({}, {}, &lv_arg));
}

}  // namespace dlcservice
