// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/dbus_adaptors/dbus_adaptor.h"
#include "dlcservice/dlc_service.h"
#include "dlcservice/mock_dlc_service.h"
#include "dlcservice/proto_utils.h"
#include "dlcservice/test_utils.h"

using base::FilePath;
using brillo::ErrorPtr;
using testing::_;
using testing::A;
using testing::ElementsAre;
using testing::Return;

namespace dlcservice {

namespace {
MATCHER_P(DlcStateEq, dlc_id, "Check the ID in DlcState.") {
  return arg.id() == dlc_id;
}
}  // namespace

class DBusServiceTest : public BaseTest {
 public:
  DBusServiceTest() {
    dlc_service_ = std::make_unique<MockDlcService>();
    dbus_service_ = std::make_unique<DBusService>(dlc_service_.get());
  }

  void SetUp() override { BaseTest::SetUp(); }

 protected:
  std::unique_ptr<MockDlcService> dlc_service_;
  std::unique_ptr<DBusService> dbus_service_;

 private:
  DBusServiceTest(const DBusServiceTest&) = delete;
  DBusServiceTest& operator=(const DBusServiceTest&) = delete;
};

TEST_F(DBusServiceTest, GetInstalled) {
  EXPECT_CALL(*dlc_service_, GetInstalled(_))
      .WillOnce(Return(DlcIdList({kFirstDlc, kSecondDlc})));

  DlcIdList ids;
  EXPECT_TRUE(dbus_service_->GetInstalled(&err_, &ids));
  EXPECT_THAT(ids, ElementsAre(kFirstDlc, kSecondDlc));
}

TEST_F(DBusServiceTest, GetInstalled2) {
  DlcBase first_dlc(kFirstDlc);
  DlcBase second_dlc(kSecondDlc);
  first_dlc.Initialize();
  second_dlc.Initialize();
  EXPECT_CALL(*dlc_service_, GetInstalled(_))
      .WillOnce(Return(DlcIdList({kFirstDlc, kSecondDlc})));
  EXPECT_CALL(*dlc_service_, GetDlc(kFirstDlc, _)).WillOnce(Return(&first_dlc));
  EXPECT_CALL(*dlc_service_, GetDlc(kSecondDlc, _))
      .WillOnce(Return(&second_dlc));

  DlcStateList dlcs;
  ListRequest request;
  request.set_check_mount(true);
  EXPECT_TRUE(dbus_service_->GetInstalled2(&err_, request, &dlcs));
  EXPECT_THAT(dlcs.states(),
              ElementsAre(DlcStateEq(kFirstDlc), DlcStateEq(kSecondDlc)));
}

TEST_F(DBusServiceTest, GetExistingDlcs) {
  EXPECT_CALL(*dlc_service_, GetExistingDlcs())
      .WillOnce(Return(DlcIdList({kSecondDlc})));

  DlcBase second_dlc(kSecondDlc);
  SetUpDlcWithSlots(kSecondDlc);
  second_dlc.Initialize();
  EXPECT_CALL(*dlc_service_, GetDlc(kSecondDlc, _))
      .WillOnce(Return(&second_dlc));

  DlcsWithContent dlc_list;
  EXPECT_TRUE(dbus_service_->GetExistingDlcs(&err_, &dlc_list));

  EXPECT_EQ(dlc_list.dlc_infos_size(), 1);
  auto second_dlc_info = dlc_list.dlc_infos()[0];
  EXPECT_EQ(second_dlc_info.id(), kSecondDlc);
  EXPECT_EQ(second_dlc_info.name(), "Second Dlc");
  EXPECT_EQ(second_dlc_info.description(), "unittest only description");
  EXPECT_EQ(second_dlc_info.used_bytes_on_disk(),
            second_dlc.GetUsedBytesOnDisk());
  EXPECT_TRUE(second_dlc_info.is_removable());
}

TEST_F(DBusServiceTest, UnloadDlCs) {
  UnloadRequest request;
  request.set_id(kFirstDlc);
  EXPECT_CALL(*dlc_service_, Unload(std::string(kFirstDlc), _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dbus_service_->Unload(&err_, request));

  auto* select = request.mutable_select();
  select->set_user_tied(true);
  select->set_scaled(false);
  EXPECT_CALL(*dlc_service_, Unload(A<const SelectDlc&>(), _, _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dbus_service_->Unload(&err_, request));
}

}  // namespace dlcservice
