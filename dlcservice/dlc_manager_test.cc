// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/time/time.h>
#include <gtest/gtest.h>

#include "dlcservice/proto_utils.h"
#include "dlcservice/test_utils.h"
#include "dlcservice/utils.h"

using dlcservice::metrics::InstallResult;
using std::string;
using testing::_;
using testing::DoAll;
using testing::ElementsAre;
using testing::Return;
using testing::SetArgPointee;

namespace dlcservice {

class DlcManagerTest : public BaseTest {
 public:
  DlcManagerTest() { dlc_manager_ = std::make_unique<DlcManager>(); }

  void Install(const DlcId& id) {
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(id, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValue(true, id, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mock_metrics_,
                SendInstallResult(InstallResult::kSuccessNewInstall));

    bool external_install_needed = false;
    EXPECT_TRUE(dlc_manager_->Install(CreateInstallRequest(id),
                                      &external_install_needed, &err_));
    CheckDlcState(id, DlcState::INSTALLING);

    InstallWithUpdateEngine({id});
    EXPECT_TRUE(dlc_manager_->InstallCompleted({id}, &err_));
    EXPECT_TRUE(dlc_manager_->FinishInstall(id, &err_));
    CheckDlcState(id, DlcState::INSTALLED);
  }

  void Uninstall(const DlcId& id) {
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
    EXPECT_TRUE(dlc_manager_->Uninstall(id, &err_));
    CheckDlcState(id, DlcState::NOT_INSTALLED);
  }

  void CheckDlcState(const DlcId& id, const DlcState::State& expected_state) {
    const auto* dlc = dlc_manager_->GetDlc(id, &err_);
    EXPECT_NE(dlc, nullptr);
    EXPECT_EQ(expected_state, dlc->GetState().state());
  }

 protected:
  std::unique_ptr<DlcManager> dlc_manager_;

 private:
  DlcManagerTest(const DlcManagerTest&) = delete;
  DlcManagerTest& operator=(const DlcManagerTest&) = delete;
};

TEST_F(DlcManagerTest, PreloadAllowedDlcTest) {
  // The third DLC has pre-loaded flag on.
  SetUpDlcPreloadedImage(kThirdDlc);
  EXPECT_CALL(*mock_system_properties_, IsOfficialBuild())
      .Times(2)
      .WillRepeatedly(Return(false));
  dlc_manager_->Initialize();

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);

  bool external_install_needed = false;
  EXPECT_TRUE(dlc_manager_->Install(CreateInstallRequest(kThirdDlc),
                                    &external_install_needed, &err_));
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre(kThirdDlc));
  EXPECT_FALSE(
      dlc_manager_->GetDlc(kThirdDlc, &err_)->GetRoot().value().empty());
  CheckDlcState(kThirdDlc, DlcState::INSTALLED);
}

TEST_F(DlcManagerTest, PreloadAllowedWithBadPreinstalledDlcTest) {
  // The third DLC has pre-loaded flag on.
  SetUpDlcWithSlots(kThirdDlc);
  SetUpDlcPreloadedImage(kThirdDlc);
  EXPECT_CALL(*mock_system_properties_, IsOfficialBuild())
      .Times(2)
      .WillRepeatedly(Return(false));
  dlc_manager_->Initialize();

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValue(true, kThirdDlc, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());
  bool external_install_needed = false;
  EXPECT_TRUE(dlc_manager_->Install(CreateInstallRequest(kThirdDlc),
                                    &external_install_needed, &err_));
  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre(kThirdDlc));
  EXPECT_FALSE(
      dlc_manager_->GetDlc(kThirdDlc, &err_)->GetRoot().value().empty());
  CheckDlcState(kThirdDlc, DlcState::INSTALLED);
}

TEST_F(DlcManagerTest, PreloadNotAllowedDlcTest) {
  SetUpDlcPreloadedImage(kSecondDlc);

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());

  dlc_manager_->Initialize();

  EXPECT_THAT(dlc_manager_->GetInstalled(), ElementsAre());
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcManagerTest, UnsupportedContentDlcRemovalCheck) {
  auto id = "unsupported-dlc";
  for (const auto& slot : {BootSlot::Slot::A, BootSlot::Slot::B}) {
    auto image_path = GetDlcImagePath(content_path_, id, kPackage, slot);
    base::CreateDirectory(image_path.DirName());
    CreateFile(image_path, 1);
  }
  EXPECT_TRUE(base::CreateDirectory(JoinPaths(prefs_path_, "dlc", id)));

  EXPECT_TRUE(base::PathExists(JoinPaths(prefs_path_, "dlc", id)));
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, id)));

  dlc_manager_->Initialize();

  EXPECT_FALSE(base::PathExists(JoinPaths(prefs_path_, "dlc", id)));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, id)));
}

TEST_F(DlcManagerTest, UnsupportedPreloadedDlcRemovalCheck) {
  auto id = "unsupported-dlc";
  auto image_path =
      JoinPaths(preloaded_content_path_, id, kPackage, kDlcImageFileName);
  base::CreateDirectory(image_path.DirName());
  CreateFile(image_path, 1);

  EXPECT_TRUE(base::PathExists(JoinPaths(preloaded_content_path_, id)));
  dlc_manager_->Initialize();
  EXPECT_FALSE(base::PathExists(JoinPaths(preloaded_content_path_, id)));
}

}  // namespace dlcservice
