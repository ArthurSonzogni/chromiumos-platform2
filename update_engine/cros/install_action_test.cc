//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/install_action.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <gtest/gtest.h>

#include "update_engine/common/action_processor.h"
#include "update_engine/common/mock_http_fetcher.h"
#include "update_engine/common/test_utils.h"
#include "update_engine/cros/fake_system_state.h"
#include "update_engine/cros/mock_dlc_utils.h"

namespace chromeos_update_engine {

namespace {
constexpr char kDefaultOffset[] = "1024";
constexpr char kDefaultSha[] =
    "5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef";
constexpr char kArtifactsMetaSomeUri[] = "some/uri/path";

constexpr char kManifestTemplate[] =
    R"({
  "critical-update": false,
  "description": "A FOOBAR DLC",
  "factory-install": false,
  "fs-type": "squashfs",
  "id": "sample-dlc",
  "image-sha256-hash": "%s",
  "image-type": "dlc",
  "is-removable": true,
  "loadpin-verity-digest": false,
  "manifest-version": 1,
  "mount-file-required": false,
  "name": "Sample DLC",
  "package": "package",
  "pre-allocated-size": "4194304",
  "preload-allowed": true,
  "reserved": false,
  "size": "%s",
  "table-sha256-hash": )"
    R"("44a4e688209bda4e06fd41aadc85a51de7d74a641275cb63b7caead96a9b03b7",
  "version": "1.0.0-r1"
})";
constexpr char kManifestWithArtifactsMetaTemplate[] =
    R"({
  "critical-update": false,
  "description": "A FOOBAR DLC",
  "factory-install": false,
  "fs-type": "squashfs",
  "id": "sample-dlc",
  "image-sha256-hash": )"
    R"("5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef",
  "image-type": "dlc",
  "is-removable": true,
  "loadpin-verity-digest": false,
  "manifest-version": 1,
  "mount-file-required": false,
  "name": "Sample DLC",
  "package": "package",
  "pre-allocated-size": "4194304",
  "preload-allowed": true,
  "reserved": false,
  "size": "1024",
  "table-sha256-hash": )"
    R"("44a4e688209bda4e06fd41aadc85a51de7d74a641275cb63b7caead96a9b03b7",
  "version": "1.0.0-r1",
  "artifacts-meta": {
    "uri": "%s"
  }
})";
constexpr char kProperties[] = R"(
CHROMEOS_RELEASE_APPID={DEB6CEFD-4EEE-462F-AC21-52DF1E17B52F}
CHROMEOS_BOARD_APPID={DEB6CEFD-4EEE-462F-AC21-52DF1E17B52F}
CHROMEOS_CANARY_APPID={90F229CE-83E2-4FAF-8479-E368A34938B1}
DEVICETYPE=CHROMEBOOK
CHROMEOS_RELEASE_NAME=Chrome OS
CHROMEOS_AUSERVER=https://tools.google.com/service/update2
CHROMEOS_DEVSERVER=
CHROMEOS_ARC_VERSION=9196679
CHROMEOS_ARC_ANDROID_SDK_VERSION=30
CHROMEOS_RELEASE_BUILDER_PATH=brya-release/R109-15201.0.0
CHROMEOS_RELEASE_KEYSET=devkeys
CHROMEOS_RELEASE_TRACK=testimage-channel
CHROMEOS_RELEASE_BUILD_TYPE=Official Build
CHROMEOS_RELEASE_DESCRIPTION=15201.0.0 (Official Build) dev-channel brya test
CHROMEOS_RELEASE_BOARD=brya
CHROMEOS_RELEASE_BRANCH_NUMBER=0
CHROMEOS_RELEASE_BUILD_NUMBER=15201
CHROMEOS_RELEASE_CHROME_MILESTONE=109
CHROMEOS_RELEASE_PATCH_NUMBER=0
CHROMEOS_RELEASE_VERSION=15201.0.0
GOOGLE_RELEASE=15201.0.0
CHROMEOS_RELEASE_UNIBUILD=1
)";

class InstallActionTestProcessorDelegate : public ActionProcessorDelegate {
 public:
  using ActionChecker = base::OnceCallback<void(AbstractAction*)>;

  InstallActionTestProcessorDelegate() : expected_code_(ErrorCode::kSuccess) {}
  ~InstallActionTestProcessorDelegate() override = default;

  void ProcessingDone(const ActionProcessor* processor,
                      ErrorCode code) override {
    brillo::MessageLoop::current()->BreakLoop();
  }

  void ActionCompleted(ActionProcessor* processor,
                       AbstractAction* action,
                       ErrorCode code) override {
    EXPECT_EQ(InstallAction::StaticType(), action->Type());
    EXPECT_EQ(expected_code_, code);

    for (auto&& ac : acs) {
      std::move(ac).Run(action);
    }
    decltype(acs)().swap(acs);
  }

  void AddActionChecker(ActionChecker ac) { acs.push_back(std::move(ac)); }

  ErrorCode expected_code_{ErrorCode::kSuccess};
  std::vector<ActionChecker> acs;
};
}  // namespace

class InstallActionTest : public ::testing::Test {
 protected:
  InstallActionTest() : data_(1024) {}
  ~InstallActionTest() override = default;

  void SetUp() override {
    loop_.SetAsCurrent();

    ASSERT_TRUE(tempdir_.CreateUniqueTempDir());
    EXPECT_TRUE(base::CreateDirectory(tempdir_.GetPath().Append("etc")));
    EXPECT_TRUE(base::CreateDirectory(
        tempdir_.GetPath().Append("dlc/foobar-dlc/package")));
    test::SetImagePropertiesRootPrefix(tempdir_.GetPath().value().c_str());
    FakeSystemState::CreateInstance();

    auto mock_http_fetcher =
        std::make_unique<MockHttpFetcher>(data_.data(), data_.size(), nullptr);
    mock_http_fetcher_ = mock_http_fetcher.get();
    install_action_ = std::make_unique<InstallAction>(
        std::move(mock_http_fetcher),
        "foobar-dlc",
        /*slotting=*/"",
        /*manifest_dir=*/tempdir_.GetPath().Append("dlc").value());
    FakeSystemState::Get()->set_dlc_utils(&mock_dlc_utils_);
  }

  base::ScopedTempDir tempdir_;

  brillo::Blob data_;
  std::unique_ptr<InstallAction> install_action_;

  InstallActionTestProcessorDelegate delegate_;

  ActionProcessor processor_;
  brillo::FakeMessageLoop loop_{nullptr};

  MockHttpFetcher* mock_http_fetcher_{nullptr};

  MockDlcUtils mock_dlc_utils_;
};

class InstallActionTestSuite : public InstallActionTest,
                               public testing::WithParamInterface<std::string> {
};

INSTANTIATE_TEST_SUITE_P(
    InstanceForManifests,
    InstallActionTestSuite,
    testing::Values(
        base::StringPrintf(kManifestTemplate, kDefaultSha, kDefaultOffset),
        base::StringPrintf(kManifestWithArtifactsMetaTemplate,
                           kArtifactsMetaSomeUri)));

TEST_P(InstallActionTestSuite, ManifestReadFailure) {
  processor_.set_delegate(&delegate_);
  processor_.EnqueueAction(std::move(install_action_));

  delegate_.expected_code_ = ErrorCode::kScaledInstallationError;
  EXPECT_CALL(mock_dlc_utils_, GetDlcManifest(testing::_, testing::_))
      .WillOnce(testing::Return(nullptr));

  loop_.PostTask(
      FROM_HERE,
      base::BindOnce(
          [](ActionProcessor* processor) { processor->StartProcessing(); },
          base::Unretained(&processor_)));
  loop_.Run();
  EXPECT_FALSE(loop_.PendingTasks());
}

TEST_P(InstallActionTestSuite, PerformSuccessfulTest) {
  processor_.set_delegate(&delegate_);
  processor_.EnqueueAction(std::move(install_action_));

  auto manifest = GetParam();
  auto manifest_ptr = std::make_shared<imageloader::Manifest>();
  manifest_ptr->ParseManifest(manifest);
  ASSERT_TRUE(test_utils::WriteFileString(
      tempdir_.GetPath().Append("etc/lsb-release").value(), kProperties));
  delegate_.expected_code_ = ErrorCode::kSuccess;

  ASSERT_TRUE(test_utils::WriteFileString(
      tempdir_.GetPath().Append("foobar-dlc-device").value(), ""));
  FakeSystemState::Get()->fake_boot_control()->SetPartitionDevice(
      "dlc/foobar-dlc/package",
      0,
      tempdir_.GetPath().Append("foobar-dlc-device").value());
  EXPECT_CALL(mock_dlc_utils_, GetDlcManifest(testing::_, testing::_))
      .WillOnce(testing::Return(manifest_ptr));

  loop_.PostTask(
      FROM_HERE,
      base::BindOnce(
          [](ActionProcessor* processor) { processor->StartProcessing(); },
          base::Unretained(&processor_)));
  loop_.Run();
  EXPECT_FALSE(loop_.PendingTasks());
}

// This also tests backup URLs.
TEST_F(InstallActionTest, PerformInvalidOffsetTest) {
  processor_.set_delegate(&delegate_);
  processor_.EnqueueAction(std::move(install_action_));

  auto manifest = base::StringPrintf(kManifestTemplate, kDefaultSha, "1025");
  auto manifest_ptr = std::make_shared<imageloader::Manifest>();
  manifest_ptr->ParseManifest(manifest);
  ASSERT_TRUE(test_utils::WriteFileString(
      tempdir_.GetPath().Append("etc/lsb-release").value(), kProperties));
  delegate_.expected_code_ = ErrorCode::kScaledInstallationError;

  ASSERT_TRUE(test_utils::WriteFileString(
      tempdir_.GetPath().Append("foobar-dlc-device").value(), ""));
  FakeSystemState::Get()->fake_boot_control()->SetPartitionDevice(
      "dlc/foobar-dlc/package",
      0,
      tempdir_.GetPath().Append("foobar-dlc-device").value());
  EXPECT_CALL(mock_dlc_utils_, GetDlcManifest(testing::_, testing::_))
      .WillOnce(testing::Return(manifest_ptr));

  loop_.PostTask(
      FROM_HERE,
      base::BindOnce(
          [](ActionProcessor* processor) { processor->StartProcessing(); },
          base::Unretained(&processor_)));
  loop_.Run();
  EXPECT_FALSE(loop_.PendingTasks());
}

TEST_F(InstallActionTest, PerformInvalidShaTest) {
  processor_.set_delegate(&delegate_);
  processor_.EnqueueAction(std::move(install_action_));

  auto manifest = base::StringPrintf(
      kManifestTemplate,
      "5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10deadbeef",
      kDefaultOffset);
  auto manifest_ptr = std::make_shared<imageloader::Manifest>();
  manifest_ptr->ParseManifest(manifest);
  ASSERT_TRUE(test_utils::WriteFileString(
      tempdir_.GetPath().Append("etc/lsb-release").value(), kProperties));
  delegate_.expected_code_ = ErrorCode::kScaledInstallationError;

  ASSERT_TRUE(test_utils::WriteFileString(
      tempdir_.GetPath().Append("foobar-dlc-device").value(), ""));
  FakeSystemState::Get()->fake_boot_control()->SetPartitionDevice(
      "dlc/foobar-dlc/package",
      0,
      tempdir_.GetPath().Append("foobar-dlc-device").value());
  EXPECT_CALL(mock_dlc_utils_, GetDlcManifest(testing::_, testing::_))
      .WillOnce(testing::Return(manifest_ptr));

  loop_.PostTask(
      FROM_HERE,
      base::BindOnce(
          [](ActionProcessor* processor) { processor->StartProcessing(); },
          base::Unretained(&processor_)));
  loop_.Run();
  EXPECT_FALSE(loop_.PendingTasks());
}

TEST_P(InstallActionTestSuite, TransferFailureFetchesFromBackup) {
  ASSERT_EQ(install_action_.get()->backup_url_index_, 0);

  processor_.set_delegate(&delegate_);
  processor_.EnqueueAction(std::move(install_action_));

  mock_http_fetcher_->FailTransfer(404);

  auto manifest = GetParam();
  auto manifest_ptr = std::make_shared<imageloader::Manifest>();
  manifest_ptr->ParseManifest(manifest);
  ASSERT_TRUE(test_utils::WriteFileString(
      tempdir_.GetPath().Append("etc/lsb-release").value(), kProperties));
  delegate_.expected_code_ = ErrorCode::kScaledInstallationError;
  delegate_.AddActionChecker(base::BindOnce([](AbstractAction* a) {
    auto* ia = reinterpret_cast<InstallAction*>(a);
    EXPECT_NE(ia->backup_url_index_, 0);
  }));

  ASSERT_TRUE(test_utils::WriteFileString(
      tempdir_.GetPath().Append("foobar-dlc-device").value(), ""));
  FakeSystemState::Get()->fake_boot_control()->SetPartitionDevice(
      "dlc/foobar-dlc/package",
      0,
      tempdir_.GetPath().Append("foobar-dlc-device").value());
  EXPECT_CALL(mock_dlc_utils_, GetDlcManifest(testing::_, testing::_))
      .WillOnce(testing::Return(manifest_ptr));

  loop_.PostTask(
      FROM_HERE,
      base::BindOnce(
          [](ActionProcessor* processor) { processor->StartProcessing(); },
          base::Unretained(&processor_)));
  loop_.Run();
  EXPECT_FALSE(loop_.PendingTasks());
}

}  // namespace chromeos_update_engine
