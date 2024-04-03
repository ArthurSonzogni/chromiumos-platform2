// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/mock_log.h>
#include <base/test/simple_test_clock.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <brillo/errors/error.h>
#include <brillo/message_loops/message_loop_utils.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lvmd/proto_bindings/lvmd.pb.h>
#include <update_engine/proto_bindings/update_engine.pb.h>

#include "dlcservice/dlc_base.h"
#include "dlcservice/dlc_base_creator.h"
#include "dlcservice/dlc_service.h"
#include "dlcservice/installer.h"
#if USE_LVM_STATEFUL_PARTITION
#include "dlcservice/lvm/dlc_lvm.h"
#include "dlcservice/lvm/dlc_lvm_creator.h"
#endif  // USE_LVM_STATEFUL_PARTITION
#include "dlcservice/mock_dlc.h"
#include "dlcservice/mock_dlc_creator.h"
#include "dlcservice/prefs.h"
#include "dlcservice/proto_utils.h"
#include "dlcservice/system_state.h"
#include "dlcservice/test_utils.h"
#include "dlcservice/types.h"
#include "dlcservice/utils.h"
#include "dlcservice/utils/mock_utils.h"
#include "dlcservice/utils/utils_interface.h"

using brillo::dbus_utils::MockDBusMethodResponse;
using dlcservice::metrics::InstallResult;
using dlcservice::metrics::UninstallResult;
using std::string;
using std::vector;
using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;
using testing::WithArgs;
using update_engine::Operation;
using update_engine::StatusResult;

namespace dlcservice {

namespace {
const DlcId kFooDlc = "foo-dlc";
}  // namespace

class DlcServiceTest : public BaseTest {
 public:
  DlcServiceTest() = default;

  void SetUp() override {
    BaseTest::SetUp();

    auto mock_dlc_creator = std::make_unique<NiceMock<MockDlcCreator>>();
    mock_dlc_creator_ptr_ = mock_dlc_creator.get();

    mock_utils_ = std::make_shared<StrictMock<MockUtils>>();

    dlc_service_ =
        std::make_unique<DlcService>(std::move(mock_dlc_creator), mock_utils_);

    mock_dlc_ = std::make_unique<MockDlc>();
    ON_CALL(*mock_dlc_, GetSanitizedId).WillByDefault(ReturnRef(kFooDlc));
  }

  void CheckDlcState(const DlcId& id,
                     const DlcState::State& expected_state,
                     const string& error_code = kErrorNone) {
    const auto* dlc = dlc_service_->GetDlc(id, &err_);
    EXPECT_NE(dlc, nullptr);
    EXPECT_EQ(expected_state, dlc->GetState().state());
    EXPECT_EQ(dlc->GetState().last_error_code(), error_code.c_str());
  }

 protected:
  std::unique_ptr<DlcService> dlc_service_;
  MockDlcCreator* mock_dlc_creator_ptr_ = nullptr;
  std::shared_ptr<MockUtils> mock_utils_;
  std::unique_ptr<MockDlc> mock_dlc_;

 private:
  DlcServiceTest(const DlcServiceTest&) = delete;
  DlcServiceTest& operator=(const DlcServiceTest&) = delete;
};

// Tests related to `Initialize`.

TEST_F(DlcServiceTest, InitializeTest) {
  // TODO(kimjae): Mock the scanning instead of depending on BaseTest setup.
  // This should make it much easier to test with.
  auto mock_dlc_1 = std::make_unique<StrictMock<MockDlc>>();
  auto mock_dlc_2 = std::make_unique<StrictMock<MockDlc>>();
  auto mock_dlc_3 = std::make_unique<StrictMock<MockDlc>>();
  auto mock_dlc_4 = std::make_unique<StrictMock<MockDlc>>();
  auto mock_dlc_scaled = std::make_unique<StrictMock<MockDlc>>();
  auto mock_dlc_force_ota = std::make_unique<StrictMock<MockDlc>>();
  auto mock_dlc_tied = std::make_unique<StrictMock<MockDlc>>();
  EXPECT_CALL(*mock_dlc_1, Initialize()).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_2, Initialize()).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_3, Initialize()).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_4, Initialize()).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_scaled, Initialize()).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_force_ota, Initialize()).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_tied, Initialize()).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_creator_ptr_, Create(_))
      .WillOnce(Return(std::move(mock_dlc_1)))
      .WillOnce(Return(std::move(mock_dlc_2)))
      .WillOnce(Return(std::move(mock_dlc_3)))
      .WillOnce(Return(std::move(mock_dlc_4)))
      .WillOnce(Return(std::move(mock_dlc_scaled)))
      .WillOnce(Return(std::move(mock_dlc_force_ota)))
      .WillOnce(Return(std::move(mock_dlc_tied)));

  EXPECT_CALL(*mock_utils_, GetSupportedDlcIds)
      .WillOnce(Return(DlcIdList({kFirstDlc, kSecondDlc, kThirdDlc, kFourthDlc,
                                  kScaledDlc, kForceOTADlc, kUserTiedDlc})));
#if USE_LVM_STATEFUL_PARTITION
  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(Return(false));
#endif  // USE_LVM_STATEFUL_PARTITION
  dlc_service_->Initialize();
}

// Tests related to `Install`.
// TODO(kimjae): Mock out between internal methods too.

TEST_F(DlcServiceTest, InstallTestUnsupported) {
  dlc_service_->SetSupportedForTesting({});

  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInvalidDlc));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).Times(1);
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
}

TEST_F(DlcServiceTest, InstallTestAlreadyInstalling) {
  EXPECT_CALL(*mock_dlc_, IsInstalling()).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
  EXPECT_TRUE(called);
}

TEST_F(DlcServiceTest, InstallTestDlcInstallFailure) {
  EXPECT_CALL(*mock_dlc_, IsInstalling()).WillOnce(Return(false));
  EXPECT_CALL(*mock_dlc_, Install(_)).WillOnce(Return(false));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  EXPECT_CALL(*mock_metrics_, SendInstallResult(InstallResult::kUnknownError));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).Times(1);
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
}

TEST_F(DlcServiceTest, InstallTestNoExternalRequirement) {
  EXPECT_CALL(*mock_dlc_, IsInstalling())
      .WillOnce(Return(false))
      // No external requirement.
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_dlc_, Install(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
  EXPECT_TRUE(called);
}

TEST_F(DlcServiceTest, InstallTestExternalRequirementUpdaterDown) {
  EXPECT_CALL(*mock_dlc_, IsInstalling())
      .WillOnce(Return(false))
      // External requirement.
      .WillOnce(Return(true))
      // For cancelling.
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, CancelInstall(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, Install(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  ON_CALL(*mock_installer_ptr_, IsReady()).WillByDefault(Return(false));
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedUpdateEngineBusy));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).Times(1);
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
}

TEST_F(DlcServiceTest, InstallTestExternalRequirementUpdaterDownCancelFailure) {
  EXPECT_CALL(*mock_dlc_, IsInstalling())
      .WillOnce(Return(false))
      // External requirement.
      .WillOnce(Return(true))
      // For cancelling (fail).
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, CancelInstall(_, _)).WillOnce(Return(false));
  EXPECT_CALL(*mock_dlc_, Install(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  ON_CALL(*mock_installer_ptr_, IsReady()).WillByDefault(Return(false));
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedUpdateEngineBusy));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).Times(1);
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
}

TEST_F(DlcServiceTest, InstallTestExternalRequirementPendingUpdate) {
  EXPECT_CALL(*mock_dlc_, IsInstalling())
      .WillOnce(Return(false))
      // External requirement.
      .WillOnce(Return(true))
      // For cancelling.
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, CancelInstall(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, Install(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  InstallerInterface::Status status;
  status.state = InstallerInterface::Status::State::BLOCKED;
  SystemState::Get()->set_installer_status(status);

  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedNeedReboot));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).Times(1);
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
}

TEST_F(DlcServiceTest,
       InstallTestExternalRequirementPendingUpdateCancelFailure) {
  EXPECT_CALL(*mock_dlc_, IsInstalling())
      .WillOnce(Return(false))
      // External requirement.
      .WillOnce(Return(true))
      // For cancelling (fail).
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, CancelInstall(_, _)).WillOnce(Return(false));
  EXPECT_CALL(*mock_dlc_, Install(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  InstallerInterface::Status status;
  status.state = InstallerInterface::Status::State::BLOCKED;
  SystemState::Get()->set_installer_status(status);

  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedNeedReboot));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).Times(1);
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
}

TEST_F(DlcServiceTest, InstallTestExternalRequirementInstallFailure) {
  EXPECT_CALL(*mock_dlc_, IsInstalling())
      .WillOnce(Return(false))
      // External requirement.
      .WillOnce(Return(true))
      // For cancelling.
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, CancelInstall(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, Install(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedUpdateEngineBusy));
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(
          WithArg<2>(Invoke([](auto&& arg) { std::move(arg).Run(nullptr); })));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).WillOnce([this](auto&& arg) {
    err_ = arg->Clone();
  });
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
  ASSERT_TRUE(err_.get());
  EXPECT_EQ(err_->GetCode(), kErrorBusy);
}

TEST_F(DlcServiceTest, InstallTestExternalRequirementInstallSuccess) {
  EXPECT_CALL(*mock_dlc_, IsInstalling())
      .WillOnce(Return(false))
      // External requirement.
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, Install(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kFooDlc), std::move(mr));
  EXPECT_TRUE(called);
}

// Tests related to `Uninstall`.

TEST_F(DlcServiceTest, UninstallTestUnsupported) {
  DlcMap supported;
  dlc_service_->SetSupportedForTesting({});

  EXPECT_CALL(*mock_metrics_,
              SendUninstallResult(UninstallResult::kFailedInvalidDlc));

  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->Uninstall(kFooDlc, &err));
}

TEST_F(DlcServiceTest, UninstallTestDlcUninstallFailure) {
  EXPECT_CALL(*mock_dlc_, Uninstall(_))
      .WillOnce(DoAll(WithArg<0>(Invoke([](brillo::ErrorPtr* err) {
                        *err =
                            Error::Create(FROM_HERE, kErrorBusy,
                                          "Install or update is in progress.");
                      })),
                      Return(false)));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  EXPECT_CALL(*mock_metrics_,
              SendUninstallResult(UninstallResult::kFailedUpdateEngineBusy));

  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->Uninstall(kFooDlc, &err));
}

TEST_F(DlcServiceTest, UninstallTestDlcUninstallSuccess) {
  EXPECT_CALL(*mock_dlc_, Uninstall(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  brillo::ErrorPtr err;
  EXPECT_TRUE(dlc_service_->Uninstall(kFooDlc, &err));
}

// Tests related to `GetDlc`.

TEST_F(DlcServiceTest, GetDlcTestUnsupported) {
  dlc_service_->SetSupportedForTesting({});

  brillo::ErrorPtr err;
  EXPECT_EQ(dlc_service_->GetDlc(kFooDlc, &err), nullptr);
  EXPECT_EQ(err->GetCode(), kErrorInvalidDlc);
}

TEST_F(DlcServiceTest, GetDlcTest) {
  auto* mock_dlc__ptr = mock_dlc_.get();

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  brillo::ErrorPtr err;
  EXPECT_EQ(dlc_service_->GetDlc(kFooDlc, &err), mock_dlc__ptr);
}

// Tests related to `GetInstalled`.

TEST_F(DlcServiceTest, GetInstalledTest) {
  EXPECT_CALL(*mock_dlc_, IsInstalled()).WillOnce(Return(true));

  auto mock_dlc_bar = std::make_unique<MockDlc>();
  EXPECT_CALL(*mock_dlc_bar, IsInstalled()).WillOnce(Return(false));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  supported.emplace("bar-dlc", std::move(mock_dlc_bar));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  const auto& dlcs = dlc_service_->GetInstalled(ListRequest());
  EXPECT_THAT(dlcs, ElementsAre(kFooDlc));
}

// Tests related to `GetExistingDlcs`.

TEST_F(DlcServiceTest, GetExistingDlcs) {
  SetUpDlcWithSlots(kFirstDlc);
  SetUpDlcWithSlots(kSecondDlc);

  DlcMap supported;
  supported.emplace(kFirstDlc, new DlcBase(kFirstDlc));
  supported.emplace(kSecondDlc, new DlcBase(kSecondDlc));
  supported.emplace(kThirdDlc, new DlcBase(kThirdDlc));
  dlc_service_->SetSupportedForTesting(std::move(supported));

#if USE_LVM_STATEFUL_PARTITION
  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(Return(false));
#endif  // USE_LVM_STATEFUL_PARTITION

  auto dlcs = dlc_service_->GetExistingDlcs();
  std::sort(std::begin(dlcs), std::end(dlcs));
  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc, kSecondDlc));
}

TEST_F(DlcServiceTest, GetExistingDlcsNoSupportOverlap) {
  SetUpDlcWithSlots(kFirstDlc);
  SetUpDlcWithSlots(kSecondDlc);

  DlcMap supported;
  supported.emplace("foo", new DlcBase("foo"));
  dlc_service_->SetSupportedForTesting(std::move(supported));

#if USE_LVM_STATEFUL_PARTITION
  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(Return(false));
#endif  // USE_LVM_STATEFUL_PARTITION

  auto dlcs = dlc_service_->GetExistingDlcs();
  std::sort(std::begin(dlcs), std::end(dlcs));
  EXPECT_THAT(dlcs, ElementsAre());
}

#if USE_LVM_STATEFUL_PARTITION
TEST_F(DlcServiceTest, GetExistingDlcsWithLogicalVolumesWithFileSupported) {
  SetUpDlcWithSlots(kFirstDlc);
  SetUpDlcWithSlots(kSecondDlc);

  DlcMap supported;
  constexpr char kLvDlcId[] = "lv-ok-dlc";
  supported.emplace(kLvDlcId, new DlcLvm(kLvDlcId));
  supported.emplace(kFirstDlc, new DlcBase(kFirstDlc));
  supported.emplace(kSecondDlc, new DlcBase(kSecondDlc));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  lvmd::LogicalVolumeList lvs;
  auto* lv = lvs.add_logical_volume();
  const std::string lv_name("dlc_lv-ok-dlc_a");
  lv->set_name(lv_name);
  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(DoAll(SetArgPointee<0>(lvs), Return(true)));
  EXPECT_CALL(*mock_utils_, LogicalVolumeNameToId(lv_name))
      .WillOnce(Return(std::string("lv-ok-dlc")));

  auto dlcs = dlc_service_->GetExistingDlcs();
  std::sort(std::begin(dlcs), std::end(dlcs));
  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc, "lv-ok-dlc", kSecondDlc));
}

TEST_F(DlcServiceTest, GetExistingDlcsWithLogicalVolumes) {
  SetUpDlcWithSlots(kFirstDlc);
  SetUpDlcWithSlots(kSecondDlc);

  DlcMap supported;
  constexpr char kLvDlcId[] = "lv-ok-dlc";
  supported.emplace(kLvDlcId, new DlcLvm(kLvDlcId));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  lvmd::LogicalVolumeList lvs;
  auto* lv = lvs.add_logical_volume();
  const std::string lv_name("dlc_lv-ok-dlc_a");
  lv->set_name(lv_name);
  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(DoAll(SetArgPointee<0>(lvs), Return(true)));
  EXPECT_CALL(*mock_utils_, LogicalVolumeNameToId(lv_name))
      .WillOnce(Return(std::string("lv-ok-dlc")));

  auto dlcs = dlc_service_->GetExistingDlcs();
  std::sort(std::begin(dlcs), std::end(dlcs));
  EXPECT_THAT(dlcs, ElementsAre("lv-ok-dlc"));
}
#endif  // USE_LVM_STATEFUL_PARTITION

// Tests related to `GetDlcsToUpdate`.

TEST_F(DlcServiceTest, GetDlcsToUpdateTest) {
  EXPECT_CALL(*mock_dlc_, MakeReadyForUpdate()).WillOnce(Return(true));

  auto mock_dlc_bar = std::make_unique<MockDlc>();
  EXPECT_CALL(*mock_dlc_bar, MakeReadyForUpdate()).WillOnce(Return(false));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  supported.emplace("bar-dlc", std::move(mock_dlc_bar));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  const auto& dlcs = dlc_service_->GetDlcsToUpdate();
  EXPECT_THAT(dlcs, ElementsAre(kFooDlc));
}

// Tests related to `InstallCompleted`.

TEST_F(DlcServiceTest, InstallCompletedTestForUnsupported) {
  dlc_service_->SetSupportedForTesting({});

  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->InstallCompleted({kFooDlc}, &err));
}

TEST_F(DlcServiceTest, InstallCompletedTestForDlcFailure) {
  EXPECT_CALL(*mock_dlc_, InstallCompleted(_)).WillOnce(Return(false));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->InstallCompleted({kFooDlc}, &err));
}

TEST_F(DlcServiceTest, InstallCompletedTestForDlcSuccess) {
  EXPECT_CALL(*mock_dlc_, InstallCompleted(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  brillo::ErrorPtr err;
  EXPECT_TRUE(dlc_service_->InstallCompleted({kFooDlc}, &err));
}

// Tests related to `UpdateCompleted`.

TEST_F(DlcServiceTest, UpdateCompletedTestForUnsupported) {
  dlc_service_->SetSupportedForTesting({});

  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->UpdateCompleted({kFooDlc}, &err));
}

TEST_F(DlcServiceTest, UpdateCompletedTestForDlcFailure) {
  EXPECT_CALL(*mock_dlc_, UpdateCompleted(_)).WillOnce(Return(false));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->UpdateCompleted({kFooDlc}, &err));
}

TEST_F(DlcServiceTest, UpdateCompletedTestForDlcSuccess) {
  EXPECT_CALL(*mock_dlc_, UpdateCompleted(_)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  brillo::ErrorPtr err;
  EXPECT_TRUE(dlc_service_->UpdateCompleted({kFooDlc}, &err));
}

// Tests related to `FinishInstall`.

TEST_F(DlcServiceTest, FinishInstallTestNothingInstalling) {
  dlc_service_->installing_dlc_id_.reset();

  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->FinishInstall(&err));
}

TEST_F(DlcServiceTest, FinishInstallTestUnsupported) {
  dlc_service_->SetSupportedForTesting({});

  dlc_service_->installing_dlc_id_ = kFooDlc;
  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->FinishInstall(&err));
  EXPECT_EQ(err->GetCode(), kErrorInvalidDlc);
}

TEST_F(DlcServiceTest, FinishInstallTestNotInstalling) {
  EXPECT_CALL(*mock_dlc_, IsInstalling()).WillOnce(Return(false));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  dlc_service_->installing_dlc_id_ = kFooDlc;
  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->FinishInstall(&err));
  EXPECT_EQ(err->GetCode(), kErrorInternal);
}

TEST_F(DlcServiceTest, FinishInstallTestSuccess) {
  EXPECT_CALL(*mock_dlc_, IsInstalling()).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  dlc_service_->installing_dlc_id_ = kFooDlc;
  brillo::ErrorPtr err;
  EXPECT_FALSE(dlc_service_->FinishInstall(&err));
}

// Tests related to `CancelInstall`.

TEST_F(DlcServiceTest, CancelInstallNoOpTest) {
  dlc_service_->installing_dlc_id_.reset();

  brillo::ErrorPtr err;
  dlc_service_->CancelInstall(err);
}

TEST_F(DlcServiceTest, CancelInstallNotInstallingResetsTest) {
  EXPECT_CALL(*mock_dlc_, IsInstalling()).WillOnce(Return(false));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));
  dlc_service_->installing_dlc_id_ = kFooDlc;

  brillo::ErrorPtr err;
  dlc_service_->CancelInstall(err);

  EXPECT_FALSE(dlc_service_->installing_dlc_id_.has_value());
}

TEST_F(DlcServiceTest, CancelInstallDlcCancelFailureResetsTest) {
  EXPECT_CALL(*mock_dlc_, IsInstalling()).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, CancelInstall(_, _)).WillOnce(Return(false));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));
  dlc_service_->installing_dlc_id_ = kFooDlc;

  brillo::ErrorPtr err;
  dlc_service_->CancelInstall(err);

  EXPECT_FALSE(dlc_service_->installing_dlc_id_.has_value());
}

TEST_F(DlcServiceTest, CancelInstallResetsTest) {
  EXPECT_CALL(*mock_dlc_, IsInstalling()).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_, CancelInstall(_, _)).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFooDlc, std::move(mock_dlc_));
  dlc_service_->SetSupportedForTesting(std::move(supported));
  dlc_service_->installing_dlc_id_ = kFooDlc;

  brillo::ErrorPtr err;
  dlc_service_->CancelInstall(err);

  EXPECT_FALSE(dlc_service_->installing_dlc_id_.has_value());
}

// Tests related to `CleanupUnsupported`.

TEST_F(DlcServiceTest, CleanupUnsupportedTest) {
  // TODO(kimjae): Mock the scanning instead of depending on BaseTest setup.
  // This should make it much easier to test with.
  dlc_service_->SetSupportedForTesting({});

  SetUpDlcWithSlots(kThirdDlc);
  EXPECT_TRUE(base::PathExists(
      GetDlcImagePath(content_path_, kThirdDlc, kPackage, BootSlot::Slot::A)));
  EXPECT_TRUE(base::PathExists(
      GetDlcImagePath(content_path_, kThirdDlc, kPackage, BootSlot::Slot::B)));

  SetUpDlcPreloadedImage(kThirdDlc);
  EXPECT_TRUE(base::PathExists(JoinPaths(preloaded_content_path_, kThirdDlc)));
#if USE_LVM_STATEFUL_PARTITION
  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(Return(false));
#endif  // USE_LVM_STATEFUL_PARTITION
  dlc_service_->CleanupUnsupported();

  EXPECT_FALSE(base::PathExists(
      GetDlcImagePath(content_path_, kThirdDlc, kPackage, BootSlot::Slot::A)));
  EXPECT_FALSE(base::PathExists(
      GetDlcImagePath(content_path_, kThirdDlc, kPackage, BootSlot::Slot::B)));
  EXPECT_FALSE(base::PathExists(JoinPaths(preloaded_content_path_, kThirdDlc)));
}

#if USE_LVM_STATEFUL_PARTITION
TEST_F(DlcServiceTest, CleanupUnsupportedLvsLvmFailure) {
  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(Return(false));
  dlc_service_->CleanupUnsupportedLvs();
}

TEST_F(DlcServiceTest, CleanupUnsupportedLvsNoDlcs) {
  lvmd::LogicalVolumeList lvs;
  for (int i = 0; i < 100; ++i) {
    auto lv_name = base::NumberToString(i) + "not-a-dlc";
    lvs.add_logical_volume()->set_name(lv_name);
    EXPECT_CALL(*mock_utils_, LogicalVolumeNameToId(lv_name));
  }
  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(DoAll(SetArgPointee<0>(lvs), Return(true)));
  dlc_service_->CleanupUnsupportedLvs();
}

TEST_F(DlcServiceTest, CleanupUnsupportedLvsAllSupportedDlcs) {
  lvmd::LogicalVolumeList lvs;
  DlcMap test_supported;
  for (int i = 0; i < 100; ++i) {
    auto dlc_name = "cow-dlc-" + base::NumberToString(i);
    auto lv_name = utils_->LogicalVolumeName(
        dlc_name, (i % 2) ? PartitionSlot::A : PartitionSlot::B);
    lvs.add_logical_volume()->set_name(lv_name);
    EXPECT_CALL(*mock_utils_, LogicalVolumeNameToId(lv_name))
        .WillOnce(Return(dlc_name));
    // Feed in strict mocks to catch unexpected calls on `DlcInterface`s.
    test_supported.emplace(dlc_name, std::make_unique<MockDlc>());
  }
  dlc_service_->SetSupportedForTesting(std::move(test_supported));

  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(DoAll(SetArgPointee<0>(lvs), Return(true)));
  dlc_service_->CleanupUnsupportedLvs();
}

TEST_F(DlcServiceTest, CleanupUnsupportedLvs) {
  lvmd::LogicalVolumeList lvs;
  constexpr int kLoop = 100;
  for (int i = 0; i < kLoop; ++i) {
    auto dlc_name = "cow-dlc-" + base::NumberToString(i);
    auto lv_name = utils_->LogicalVolumeName(
        dlc_name, (i % 3) ? PartitionSlot::A : PartitionSlot::B);
    lvs.add_logical_volume()->set_name(lv_name);
    EXPECT_CALL(*mock_utils_, LogicalVolumeNameToId(lv_name))
        .WillOnce(Return(dlc_name));
  }

  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
      .WillOnce(DoAll(SetArgPointee<0>(lvs), Return(true)));
  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, RemoveLogicalVolumesAsync(_, _));
  dlc_service_->CleanupUnsupportedLvs();
}
#endif  // USE_LVM_STATEFUL_PARTITION

// Tests related to `OnInstallFailure`.

TEST_F(DlcServiceTest, OnInstallFailure) {
  auto assert_cb = [](brillo::ErrorPtr) {
    ASSERT_TRUE(false) << "Should not be called.";
  };
  auto err_ptr = brillo::Error::CreateNoLog(FROM_HERE, "(domain)", "(code)",
                                            "(msg)", nullptr);
  dlc_service_->OnInstallFailure(base::BindOnce(assert_cb), err_ptr.get());
}

TEST_F(DlcServiceTest, UnloadDlcs) {
  auto mock_dlc_1 = std::make_unique<StrictMock<MockDlc>>();
  EXPECT_CALL(*mock_dlc_1, IsUserTied).WillOnce(Return(false));

  auto mock_dlc_user = std::make_unique<StrictMock<MockDlc>>();
  EXPECT_CALL(*mock_dlc_user, IsUserTied).WillOnce(Return(true));
  EXPECT_CALL(*mock_dlc_user, Unload).WillOnce(Return(true));

  DlcMap supported;
  supported.emplace(kFirstDlc, std::move(mock_dlc_1));
  supported.emplace(kUserTiedDlc, std::move(mock_dlc_user));
  dlc_service_->SetSupportedForTesting(std::move(supported));

  CreateDir(JoinPaths(mount_path_, kFirstDlc, kPackage));
  CreateDir(JoinPaths(mount_path_, kUserTiedDlc, kPackage));
  CreateDir(JoinPaths(mount_path_, "not-a-dlc"));

  SelectDlc select;
  select.set_user_tied(true);
  brillo::ErrorPtr tmp_err;
  EXPECT_TRUE(dlc_service_->Unload(select, mount_path_, &tmp_err));
}

// NOTE: Do not add new code below this line.
//
// Everything below is legacy method of testing.

class DlcServiceTestLegacy : public BaseTest {
 public:
  DlcServiceTestLegacy() = default;

  void SetUp() override {
    BaseTest::SetUp();

    InitializeDlcService();
  }

  void InitializeDlcService() {
    auto dlc_creator =
#if USE_LVM_STATEFUL_PARTITION
        std::make_unique<DlcLvmCreator>();
    EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ListLogicalVolumes(_))
        .WillOnce(Return(false));
    EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, GetLogicalVolumePath(_))
        .WillRepeatedly(Return(""));
#else
        std::make_unique<DlcBaseCreator>();
#endif  // USE_LVM_STATEFUL_PARTITION

    mock_utils_ = std::make_shared<StrictMock<MockUtils>>();

    dlc_service_ =
        std::make_unique<DlcService>(std::move(dlc_creator), mock_utils_);

    EXPECT_CALL(*mock_utils_, GetSupportedDlcIds)
        .WillOnce(Return(DlcIdList({kFirstDlc, kSecondDlc, kThirdDlc,
                                    kFourthDlc, kScaledDlc, kUserTiedDlc})));
    dlc_service_->Initialize();
  }

  // Successfully install a DLC.
  void Install(const DlcId& id) {
    EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
        .WillOnce(DoAll(
            WithArg<0>(Invoke([this](const auto& ia) {
              InstallViaInstaller({ia.id});
              brillo::ErrorPtr err;
              dlc_service_->InstallCompleted({ia.id}, &err);
            })),
            WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); }))));
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(mount_path_.value()), Return(true)));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
    EXPECT_CALL(*mock_update_engine_proxy_ptr_,
                SetDlcActiveValueAsync(true, id, _, _, _))
        .Times(1);
    EXPECT_CALL(*mock_metrics_,
                SendInstallResult(InstallResult::kSuccessNewInstall));

    auto mr = std::make_unique<MockDBusMethodResponse<>>();
    bool called = false;
    mr->set_return_callback(
        base::BindOnce([](bool* called) { *called = true; }, &called));
    dlc_service_->Install(CreateInstallRequest(id), std::move(mr));
    EXPECT_TRUE(called);

    CheckDlcState(id, DlcState::INSTALLING);

    dlc_service_->OnStatusSync(InstallerInterface::Status{
        .state = InstallerInterface::Status::State::OK,
        .is_install = true,
    });

    CheckDlcState(id, DlcState::INSTALLED);
  }

  void CheckDlcState(const DlcId& id,
                     const DlcState::State& expected_state,
                     const string& error_code = kErrorNone) {
    const auto* dlc = dlc_service_->GetDlc(id, &err_);
    EXPECT_NE(dlc, nullptr);
    EXPECT_EQ(expected_state, dlc->GetState().state());
    EXPECT_EQ(dlc->GetState().last_error_code(), error_code.c_str());
  }

 protected:
  std::unique_ptr<DlcService> dlc_service_;
  std::shared_ptr<MockUtils> mock_utils_;

 private:
  DlcServiceTestLegacy(const DlcServiceTestLegacy&) = delete;
  DlcServiceTestLegacy& operator=(const DlcServiceTestLegacy&) = delete;
};

TEST_F(DlcServiceTestLegacy, GetInstalledTest) {
  Install(kFirstDlc);

  const auto& dlcs = dlc_service_->GetInstalled(ListRequest());

  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc));
  EXPECT_FALSE(
      dlc_service_->GetDlc(kFirstDlc, &err_)->GetRoot().value().empty());
}

TEST_F(DlcServiceTestLegacy, GetDlcsToUpdateTest) {
  Install(kFirstDlc);

  // Make second DLC marked as verified so we can get it in the list of DLCs
  // needed to be updated.
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));
  const auto& dlcs = dlc_service_->GetDlcsToUpdate();

  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc, kSecondDlc));
}

#if USE_LVM_STATEFUL_PARTITION
TEST_F(DlcServiceTestLegacy, GetDlcsToUpdateLogicalVolumeTest) {
  Install(kFirstDlc);

  // Make fourth DLC marked as verified so we can get it in the list of DLCs
  // needed to be updated.
  EXPECT_TRUE(dlc_service_->InstallCompleted({kFourthDlc}, &err_));

  EXPECT_CALL(*mock_lvmd_proxy_wrapper_ptr_, ActivateLogicalVolume(_))
      .WillOnce(Return(true));
  const auto& dlcs = dlc_service_->GetDlcsToUpdate();

  EXPECT_THAT(dlcs, ElementsAre(kFirstDlc, kFourthDlc));
}
#endif  // USE_LVM_STATEFUL_PARTITION

TEST_F(DlcServiceTestLegacy,
       GetInstalledMimicDlcserviceRebootWithoutVerifiedStamp) {
  Install(kFirstDlc);
  const auto& dlcs_before = dlc_service_->GetInstalled(ListRequest());
  EXPECT_THAT(dlcs_before, ElementsAre(kFirstDlc));
  EXPECT_FALSE(
      dlc_service_->GetDlc(kFirstDlc, &err_)->GetRoot().value().empty());

  // Create |kSecondDlc| image, but not verified after device reboot.
  SetUpDlcWithSlots(kSecondDlc);

  const auto& dlcs_after = dlc_service_->GetInstalled(ListRequest());
  EXPECT_THAT(dlcs_after, ElementsAre(kFirstDlc));
}

// TODO(kimjae): Deprecate DLC used by indicators.
TEST_F(DlcServiceTestLegacy, UninstallTestForUserDlc) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  // Uninstall should set the DLC inactive.
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(false, kFirstDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  auto dlc_prefs_path = prefs_path_.Append("dlc").Append(kFirstDlc);
  EXPECT_TRUE(base::PathExists(dlc_prefs_path));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(err_.get() == nullptr);
  // Uninstall should delete the DLC right away.
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  EXPECT_FALSE(base::PathExists(dlc_prefs_path));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
  // Uninstall should change the verified status.
  EXPECT_FALSE(dlc_service_->GetDlc(kFirstDlc, &err_)->IsVerified());
}

TEST_F(DlcServiceTestLegacy, UninstallNotInstalledIsValid) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(false, kSecondDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  EXPECT_TRUE(dlc_service_->Uninstall(kSecondDlc, &err_));
  EXPECT_TRUE(err_.get() == nullptr);
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTestLegacy, UninstallFailToSetDlcActiveValueFalse) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(false, kFirstDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTestLegacy, UninstallInvalidDlcTest) {
  // Setup a mock logger to ensure alert is printed on a failed uninstall
  base::test::MockLog mock_log;
  mock_log.StartCapturingLogs();

  const auto& id = "invalid-dlc-id";
  EXPECT_CALL(*mock_metrics_,
              SendUninstallResult(UninstallResult::kFailedInvalidDlc));
  // Logger expectations.
  EXPECT_CALL(mock_log, Log(_, _, _, _, _)).Times(AnyNumber());
  EXPECT_CALL(mock_log,
              Log(::logging::LOGGING_ERROR, _, _, _,
                  HasSubstr(AlertLogTag(kCategoryUninstall).c_str())));

  EXPECT_FALSE(dlc_service_->Uninstall(id, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorInvalidDlc);
}

TEST_F(DlcServiceTestLegacy, UninstallImageLoaderFailureTest) {
  Install(kFirstDlc);

  // |ImageLoader| not available.
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(false, kFirstDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(err_.get() == nullptr);
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTestLegacy, UninstallUpdateEngineBusyFailureTest) {
  Install(kFirstDlc);

  InstallerInterface::Status status;
  status.state = InstallerInterface::Status::State::CHECKING;
  SystemState::Get()->set_installer_status(status);
  EXPECT_CALL(*mock_metrics_,
              SendUninstallResult(UninstallResult::kFailedUpdateEngineBusy));

  EXPECT_FALSE(dlc_service_->Uninstall(kFirstDlc, &err_));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTestLegacy, UninstallInstallingFails) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendUninstallResult(UninstallResult::kFailedUpdateEngineBusy));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_FALSE(dlc_service_->Uninstall(kSecondDlc, &err_));
  EXPECT_EQ(err_->GetCode(), kErrorBusy);
}

TEST_F(DlcServiceTestLegacy, UninstallInstallingButInstalledFails) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, UnloadDlcImage(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_, SendUninstallResult(UninstallResult::kSuccess));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // |kFirstDlc| was installed, so there should be no problem uninstalling it
  // |even if |kSecondDlc| is installing.
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(false, kFirstDlc, _, _, _))
      .Times(1);
  EXPECT_TRUE(dlc_service_->Uninstall(kFirstDlc, &err_));
  EXPECT_TRUE(err_.get() == nullptr);
  CheckDlcState(kFirstDlc, DlcState::NOT_INSTALLED);
}

TEST_F(DlcServiceTestLegacy, InstallInvalidDlcTest) {
  const string id = "bad-dlc-id";
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInvalidDlc));
  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).WillOnce([this](auto&& arg) {
    err_ = arg->Clone();
  });
  dlc_service_->Install(CreateInstallRequest(id), std::move(mr));
  ASSERT_TRUE(err_.get());
  EXPECT_EQ(err_->GetCode(), kErrorInvalidDlc);
}

TEST_F(DlcServiceTestLegacy, InstallTest) {
  Install(kFirstDlc);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  EXPECT_THAT(dlc_service_->GetInstalled(ListRequest()),
              ElementsAre(kFirstDlc));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // Should remain same as it's not stamped verified.
  EXPECT_THAT(dlc_service_->GetInstalled(ListRequest()),
              ElementsAre(kFirstDlc));

  // TODO(ahassani): Add more install process liked |InstallCompleted|, etc.
}

TEST_F(DlcServiceTestLegacy, InstallAlreadyInstalledValid) {
  Install(kFirstDlc);

  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(true, kFirstDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kFirstDlc), std::move(mr));
  EXPECT_TRUE(called);
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kFirstDlc)));
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTestLegacy, InstallAlreadyInstalledWhileAnotherInstalling) {
  Install(kFirstDlc);

  // Keep |kSecondDlc| installing.
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  {
    auto mr = std::make_unique<MockDBusMethodResponse<>>();
    bool called = false;
    mr->set_return_callback(
        base::BindOnce([](bool* called) { *called = true; }, &called));
    dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
    EXPECT_TRUE(called);
  }
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // |kFirstDlc| can quickly be installed again even though another install is
  // ongoing.
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(true, kFirstDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));

  {
    auto mr = std::make_unique<MockDBusMethodResponse<>>();
    bool called = false;
    mr->set_return_callback(
        base::BindOnce([](bool* called) { *called = true; }, &called));
    dlc_service_->Install(CreateInstallRequest(kFirstDlc), std::move(mr));
    EXPECT_TRUE(called);
  }
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTestLegacy, InstallCannotSetDlcActiveValue) {
  SetMountPath(mount_path_.value());
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(true, kSecondDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = true,
  });

  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTestLegacy, PeriodicInstallCheck) {
  vector<InstallerInterface::Status> status_list;
  for (const auto& state : {InstallerInterface::Status::State::CHECKING,
                            InstallerInterface::Status::State::DOWNLOADING}) {
    status_list.push_back(decltype(status_list)::value_type{
        .state = state,
        .is_install = true,
    });
  }
  EXPECT_CALL(*mock_installer_ptr_, StatusSync())
      .WillOnce(Invoke([this, status_list]() {
        dlc_service_->OnStatusSync(status_list[0]);
      }))
      .WillOnce(Invoke([this, status_list]() {
        dlc_service_->OnStatusSync(status_list[0]);
      }))
      .WillOnce(Invoke([this, status_list]() {
        dlc_service_->OnStatusSync(status_list[1]);
      }));

  // We need to make sure the state is installing so, rescheduling periodic
  // check happens.
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // The first time it should not get the status because enough time hasn't
  // passed yet.
  dlc_service_->SchedulePeriodicInstallCheck();
  EXPECT_EQ(SystemState::Get()->installer_status().state,
            InstallerInterface::Status::State::OK);

  // Now advance clock and make sure that first time we do get status.
  clock_.Advance(base::Seconds(11));
  loop_.RunOnce(false);
  EXPECT_EQ(SystemState::Get()->installer_status().state,
            InstallerInterface::Status::State::CHECKING);

  // Now advance the clock even more, this time fail the get status. The status
  // should remain same.
  clock_.Advance(base::Seconds(11));
  loop_.RunOnce(false);
  EXPECT_EQ(SystemState::Get()->installer_status().state,
            InstallerInterface::Status::State::CHECKING);

  // Now advance a little bit more to see we got the new status.
  clock_.Advance(base::Seconds(11));
  loop_.RunOnce(false);
  EXPECT_EQ(SystemState::Get()->installer_status().state,
            InstallerInterface::Status::State::DOWNLOADING);
}

TEST_F(DlcServiceTestLegacy, InstallSchedulesPeriodicInstallCheck) {
  vector<InstallerInterface::Status> status_list;
  for (const auto& state : {InstallerInterface::Status::State::CHECKING,
                            InstallerInterface::Status::State::OK}) {
    status_list.push_back(decltype(status_list)::value_type{
        .state = state,
        .is_install = true,
    });
  }

  EXPECT_CALL(*mock_installer_ptr_, StatusSync())
      .WillOnce(Invoke([this, status_list]() {
        dlc_service_->OnStatusSync(status_list[1]);
      }));
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedToVerifyImage));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // The checking for update comes from signal.
  dlc_service_->OnStatusSync(status_list[0]);

  // Now advance clock and make sure that periodic install check is scheduled
  // and eventually called.
  clock_.Advance(base::Seconds(11));
  loop_.RunOnce(false);

  // Since the update_engine status went back to IDLE, the install is complete
  // and it should fail.
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTestLegacy, InstallFailureCleansUp) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(
          WithArg<2>(Invoke([](auto&& arg) { std::move(arg).Run(nullptr); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedUpdateEngineBusy));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).WillOnce([this](auto&& arg) {
    err_ = arg->Clone();
  });
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  ASSERT_TRUE(err_.get());
  EXPECT_EQ(err_->GetCode(), kErrorBusy);

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorBusy);
}

TEST_F(DlcServiceTestLegacy, InstallUrlTest) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(
          DoAll(WithArg<0>(Invoke([](const auto& arg) {
                  EXPECT_EQ(arg.url, kDefaultOmahaUrl);
                })),
                WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); }))));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc, kDefaultOmahaUrl),
                        std::move(mr));
  EXPECT_TRUE(called);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
}

TEST_F(DlcServiceTestLegacy, InstallAlreadyInstalledThatGotUnmountedTest) {
  Install(kFirstDlc);

  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
  const auto mount_path_root = JoinPaths(mount_path_, "root");
  EXPECT_TRUE(base::PathExists(mount_path_root));
  EXPECT_TRUE(base::DeletePathRecursively(mount_path_root));

  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(mount_path_.value()), Return(true)));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(true, kFirstDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessAlreadyInstalled));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kFirstDlc), std::move(mr));
  EXPECT_TRUE(called);
  CheckDlcState(kFirstDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTestLegacy, InstallFailsToCreateDirectory) {
  base::SetPosixFilePermissions(content_path_, 0444);
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(1);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedToCreateDirectory));

  // Install will fail because DlcBase::CreateDlc() will fail to create
  // directories inside |content_path_|, since the permissions don't allow
  // writing into |content_path_|.
  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  EXPECT_CALL(*mr, ReplyWithError(_)).WillOnce([this](auto&& arg) {
    err_ = arg->Clone();
  });
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  ASSERT_TRUE(err_.get());
  EXPECT_EQ(err_->GetCode(), kErrorInternal);

  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTestLegacy, OnStatusUpdateSignalDlcRootTest) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(true, kSecondDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = true,
  });

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLED);

  const auto& dlcs_after = dlc_service_->GetInstalled(ListRequest());

  EXPECT_THAT(dlcs_after, ElementsAre(kFirstDlc, kSecondDlc));
  EXPECT_FALSE(
      dlc_service_->GetDlc(kFirstDlc, &err_)->GetRoot().value().empty());
  EXPECT_FALSE(
      dlc_service_->GetDlc(kSecondDlc, &err_)->GetRoot().value().empty());
}

TEST_F(DlcServiceTestLegacy, OnStatusUpdateSignalNoRemountTest) {
  Install(kFirstDlc);

  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(true, kSecondDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = true,
  });
}

TEST_F(DlcServiceTestLegacy, OnStatusUpdateSignalTest) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(true, kSecondDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = true,
  });

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTestLegacy, MountFailureTest) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(""), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedToMountImage));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = true,
  });

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  EXPECT_FALSE(dlc_service_->GetDlc(kSecondDlc, &err_)->IsVerified());
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTestLegacy, ReportingFailureCleanupTest) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInstallInUpdateEngine));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::ERROR,
      .is_install = true,
  });
  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = true,
  });

  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTestLegacy, ReportingFailureSignalTest) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInstallInUpdateEngine));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::ERROR,
      .is_install = true,
  });
  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = true,
  });

  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTestLegacy, SignalToleranceCapTest) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInstallInUpdateEngine));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  for (int i = 0; i < 30; ++i) {
    dlc_service_->OnStatusSync(InstallerInterface::Status{
        .state = InstallerInterface::Status::State::OK,
        .is_install = false,
    });
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
    CheckDlcState(kSecondDlc, DlcState::INSTALLING);
  }

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = false,
  });
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTestLegacy, SignalToleranceCapResetTest) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kFailedInstallInUpdateEngine));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);

  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  for (int i = 0; i < 30; ++i) {
    dlc_service_->OnStatusSync(InstallerInterface::Status{
        .state = InstallerInterface::Status::State::OK,
        .is_install = false,
    });
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
    CheckDlcState(kSecondDlc, DlcState::INSTALLING);
  }
  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::VERIFYING,
      .is_install = true,
  });
  EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  // A good status handle should reset the tolerance count.
  for (int i = 0; i < 30; ++i) {
    dlc_service_->OnStatusSync(InstallerInterface::Status{
        .state = InstallerInterface::Status::State::OK,
        .is_install = false,
    });
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
    CheckDlcState(kSecondDlc, DlcState::INSTALLING);
  }

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = false,
  });
  EXPECT_FALSE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
  CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
}

TEST_F(DlcServiceTestLegacy, OnStatusUpdateSignalDownloadProgressTest) {
  EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
      .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              SetDlcActiveValueAsync(true, kSecondDlc, _, _, _))
      .Times(1);
  EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(mount_path_.value()), Return(true)));
  EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
  EXPECT_CALL(*mock_metrics_,
              SendInstallResult(InstallResult::kSuccessNewInstall));

  auto mr = std::make_unique<MockDBusMethodResponse<>>();
  bool called = false;
  mr->set_return_callback(
      base::BindOnce([](bool* called) { *called = true; }, &called));
  dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
  EXPECT_TRUE(called);
  CheckDlcState(kSecondDlc, DlcState::INSTALLING);

  const vector<Installer::InstallerInterface::Status::State> state_sequence = {
      InstallerInterface::Status::State::CHECKING,
      InstallerInterface::Status::State::VERIFYING};
  for (const auto& state : state_sequence) {
    dlc_service_->OnStatusSync(InstallerInterface::Status{
        .state = state,
        .is_install = true,
    });
  }

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::DOWNLOADING,
      .is_install = true,
  });

  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

  dlc_service_->OnStatusSync(InstallerInterface::Status{
      .state = InstallerInterface::Status::State::OK,
      .is_install = true,
  });

  CheckDlcState(kSecondDlc, DlcState::INSTALLED);
}

TEST_F(DlcServiceTestLegacy,
       OnStatusUpdateSignalSubsequentialBadOrNonInstalledDlcsNonBlocking) {
  for (int i = 0; i < 5; i++) {
    EXPECT_CALL(*mock_installer_ptr_, Install(_, _, _))
        .WillOnce(WithArg<1>(Invoke([](auto&& arg) { std::move(arg).Run(); })));
    EXPECT_CALL(*mock_image_loader_proxy_ptr_, LoadDlc(_, _, _, _))
        .WillOnce(Return(false));
    EXPECT_CALL(mock_state_change_reporter_, DlcStateChanged(_)).Times(2);
    EXPECT_CALL(*mock_metrics_,
                SendInstallResult(InstallResult::kFailedToMountImage));

    auto mr = std::make_unique<MockDBusMethodResponse<>>();
    bool called = false;
    mr->set_return_callback(
        base::BindOnce([](bool* called) { *called = true; }, &called));
    dlc_service_->Install(CreateInstallRequest(kSecondDlc), std::move(mr));
    EXPECT_TRUE(called);
    CheckDlcState(kSecondDlc, DlcState::INSTALLING);

    EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));

    dlc_service_->OnStatusSync(InstallerInterface::Status{
        .state = InstallerInterface::Status::State::OK,
        .is_install = true,
    });
    EXPECT_TRUE(base::PathExists(JoinPaths(content_path_, kSecondDlc)));
    CheckDlcState(kSecondDlc, DlcState::NOT_INSTALLED, kErrorInternal);
  }
}

TEST_F(DlcServiceTestLegacy, InstallCompleted) {
  EXPECT_TRUE(dlc_service_->InstallCompleted({kSecondDlc}, &err_));
  EXPECT_TRUE(dlc_service_->GetDlc(kSecondDlc, &err_)->IsVerified());
}

TEST_F(DlcServiceTestLegacy, UpdateCompleted) {
  auto inactive_boot_slot = SystemState::Get()->inactive_boot_slot();
  EXPECT_FALSE(
      Prefs(DlcBase(kSecondDlc), inactive_boot_slot).Exists(kDlcPrefVerified));
  EXPECT_TRUE(dlc_service_->UpdateCompleted({kFirstDlc, kSecondDlc}, &err_));
  EXPECT_TRUE(
      Prefs(DlcBase(kSecondDlc), inactive_boot_slot).Exists(kDlcPrefVerified));
}

}  // namespace dlcservice
