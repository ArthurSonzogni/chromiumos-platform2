// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lvmd/lvmd.h"

#include <optional>
#include <tuple>

#include <brillo/blkdev_utils/lvm_device.h>
#include <brillo/blkdev_utils/mock_lvm.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lvmd/proto_bindings/lvmd.pb.h>

using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Ref;
using ::testing::Return;

namespace lvmd {

MATCHER_P2(DictValueMatch, key, value, "") {
  return *arg.FindString(key) == value;
}

class LvmdTest : public ::testing::Test {
 public:
  LvmdTest() = default;
  LvmdTest(const LvmdTest&) = delete;
  LvmdTest& operator=(const LvmdTest&) = delete;

  void SetUp() override {
    auto lvm = std::make_unique<brillo::MockLogicalVolumeManager>();
    lvm_ptr_ = lvm.get();

    lvmd_ = std::make_unique<Lvmd>(std::move(lvm));
  }

 protected:
  brillo::MockLogicalVolumeManager* lvm_ptr_;

  std::unique_ptr<Lvmd> lvmd_;
};

TEST_F(LvmdTest, CreateLogicalVolumesEmpty) {
  brillo::ErrorPtr err;
  CreateLogicalVolumesRequest request;
  CreateLogicalVolumesResponse response;
  EXPECT_TRUE(lvmd_->CreateLogicalVolumes(&err, request, &response));
}

TEST_F(LvmdTest, CreateLogicalVolumesLvmCallCheck) {
  brillo::ErrorPtr err;
  CreateLogicalVolumesRequest request;
  CreateLogicalVolumesResponse response;

  std::ignore = request.add_logical_volume_infos();

  auto opt_lv = std::make_optional<brillo::LogicalVolume>("", "", nullptr);
  EXPECT_CALL(*lvm_ptr_, CreateLogicalVolume(_, _, _)).WillOnce(Return(opt_lv));

  EXPECT_TRUE(lvmd_->CreateLogicalVolumes(&err, request, &response));
}

TEST_F(LvmdTest, CreateLogicalVolumesLvmFailureCheck) {
  brillo::ErrorPtr err;
  CreateLogicalVolumesRequest request;
  CreateLogicalVolumesResponse response;

  std::ignore = request.add_logical_volume_infos();

  std::optional<brillo::LogicalVolume> opt_lv;
  EXPECT_CALL(*lvm_ptr_, CreateLogicalVolume(_, _, _)).WillOnce(Return(opt_lv));

  EXPECT_FALSE(lvmd_->CreateLogicalVolumes(&err, request, &response));
}

TEST_F(LvmdTest, CreateLogicalVolumesSuccessfulLvsPopulated) {
  brillo::ErrorPtr err;
  CreateLogicalVolumesRequest request;
  CreateLogicalVolumesResponse response;

  for (const auto& name : {"lv1", "lv2", "some-more-lv1", "some-more-lv2"}) {
    auto* lv_info = request.add_logical_volume_infos();
    lv_info->mutable_lv_config()->set_name(name);
  }

  {
    InSequence seq;
    {
      std::optional<brillo::LogicalVolume> opt_lv;
      EXPECT_CALL(*lvm_ptr_,
                  CreateLogicalVolume(_, _, DictValueMatch("name", "lv1")))
          .WillOnce((Return(opt_lv)));
    }
    {
      auto opt_lv =
          std::make_optional<brillo::LogicalVolume>("lv2", "some-vg", nullptr);
      EXPECT_CALL(*lvm_ptr_,
                  CreateLogicalVolume(_, _, DictValueMatch("name", "lv2")))
          .WillOnce((Return(opt_lv)));
    }
    {
      std::optional<brillo::LogicalVolume> opt_lv;
      EXPECT_CALL(*lvm_ptr_, CreateLogicalVolume(_, _, _))
          .WillRepeatedly((Return(opt_lv)));
    }

    EXPECT_FALSE(lvmd_->CreateLogicalVolumes(&err, request, &response));
  }

  ASSERT_TRUE(response.has_logical_volume_list());
  EXPECT_EQ(response.logical_volume_list().logical_volume_size(), 1);
  EXPECT_EQ(response.logical_volume_list().logical_volume().at(0).name(),
            "lv2");
}

TEST_F(LvmdTest, RemoveLogicalVolumesEmpty) {
  brillo::ErrorPtr err;
  RemoveLogicalVolumesRequest request;
  RemoveLogicalVolumesResponse response;
  EXPECT_TRUE(lvmd_->RemoveLogicalVolumes(&err, request, &response));
}

TEST_F(LvmdTest, RemoveLogicalVolumesLvmCallCheck) {
  brillo::ErrorPtr err;
  RemoveLogicalVolumesRequest request;
  RemoveLogicalVolumesResponse response;

  std::ignore = request.mutable_logical_volume_list()->add_logical_volume();

  EXPECT_CALL(*lvm_ptr_, RemoveLogicalVolume(_, _)).WillOnce((Return(true)));

  EXPECT_TRUE(lvmd_->RemoveLogicalVolumes(&err, request, &response));
}

TEST_F(LvmdTest, RemoveLogicalVolumesLvmFailureCheck) {
  brillo::ErrorPtr err;
  RemoveLogicalVolumesRequest request;
  RemoveLogicalVolumesResponse response;

  std::ignore = request.mutable_logical_volume_list()->add_logical_volume();

  EXPECT_CALL(*lvm_ptr_, RemoveLogicalVolume(_, _)).WillOnce((Return(false)));

  EXPECT_FALSE(lvmd_->RemoveLogicalVolumes(&err, request, &response));
}

TEST_F(LvmdTest, RemoveLogicalVolumesFailedLvsPopulated) {
  brillo::ErrorPtr err;
  RemoveLogicalVolumesRequest request;
  RemoveLogicalVolumesResponse response;

  for (const auto& name : {"lv1", "lv2"}) {
    auto* lv = request.mutable_logical_volume_list()->add_logical_volume();
    lv->set_name(name);
  }

  {
    InSequence seq;
    EXPECT_CALL(*lvm_ptr_, RemoveLogicalVolume(_, "lv1"))
        .WillOnce((Return(true)));
    EXPECT_CALL(*lvm_ptr_, RemoveLogicalVolume(_, "lv2"))
        .WillOnce(Return(false));

    EXPECT_FALSE(lvmd_->RemoveLogicalVolumes(&err, request, &response));
  }

  ASSERT_TRUE(response.has_logical_volume_list());
  EXPECT_EQ(response.logical_volume_list().logical_volume_size(), 1);
  EXPECT_EQ(response.logical_volume_list().logical_volume().at(0).name(),
            "lv2");
}

}  // namespace lvmd
