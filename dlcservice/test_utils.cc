// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/test_utils.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <imageloader/proto_bindings/imageloader.pb.h>
#include <imageloader/dbus-proxy-mocks.h>
#if USE_LVM_STATEFUL_PARTITION
#include <lvmd/proto_bindings/lvmd.pb.h>
// NOLINTNEXTLINE(build/include_alpha)
#include <lvmd/dbus-proxy-mocks.h>
#endif  // USE_LVM_STATEFUL_PARTITION
#include <metrics/metrics_library_mock.h>
#include <update_engine/dbus-constants.h>
#include <update_engine/dbus-proxy-mocks.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/mock_installer.h"
#if USE_LVM_STATEFUL_PARTITION
#include "dlcservice/lvm/mock_lvmd_proxy_wrapper.h"
#endif  // USE_LVM_STATEFUL_PARTITION
#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"
#include "dlcservice/utils/utils.h"

using std::string;
using std::vector;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace dlcservice {

const char kForceOTADlc[] = "forceota-dlc";

const char kFirstDlc[] = "first-dlc";
const char kSecondDlc[] = "second-dlc";
const char kThirdDlc[] = "third-dlc";
const char kFourthDlc[] = "fourth-dlc";
const char kScaledDlc[] = "scaled-dlc";
const char kUserTiedDlc[] = "user-tied-dlc";
const char kDefaultOmahaUrl[] = "http://foo-url";

BaseTest::BaseTest() {
  // Create mocks with default behaviors.
#if USE_LVM_STATEFUL_PARTITION
  mock_lvmd_proxy_wrapper_ =
      std::make_unique<StrictMock<MockLvmdProxyWrapper>>();
  mock_lvmd_proxy_wrapper_ptr_ = mock_lvmd_proxy_wrapper_.get();
#endif  // USE_LVM_STATEFUL_PARTITION

  mock_image_loader_proxy_ =
      std::make_unique<StrictMock<ImageLoaderProxyMock>>();
  mock_image_loader_proxy_ptr_ = mock_image_loader_proxy_.get();

  mock_bus_ = new dbus::MockBus(dbus::Bus::Options{});
  mock_update_engine_object_proxy_ = new dbus::MockObjectProxy(
      mock_bus_.get(), update_engine::kUpdateEngineServiceName,
      dbus::ObjectPath(update_engine::kUpdateEngineServicePath));

  mock_update_engine_proxy_ =
      std::make_unique<StrictMock<UpdateEngineProxyMock>>();
  mock_update_engine_proxy_ptr_ = mock_update_engine_proxy_.get();

  mock_installer_ = std::make_unique<MockInstaller>();
  mock_installer_ptr_ = mock_installer_.get();

  mock_session_manager_proxy_ = std::make_unique<SessionManagerProxyMock>();
  mock_session_manager_proxy_ptr_ = mock_session_manager_proxy_.get();

  mock_boot_slot_ = std::make_unique<MockBootSlot>();
  mock_boot_slot_ptr_ = mock_boot_slot_.get();
}

void BaseTest::SetUp() {
  loop_.SetAsCurrent();

  SetUpFilesAndDirectories();

  auto mock_metrics = std::make_unique<testing::StrictMock<MockMetrics>>();
  mock_metrics_ = mock_metrics.get();

  auto mock_system_properties =
      std::make_unique<testing::StrictMock<MockSystemProperties>>();
  mock_system_properties_ = mock_system_properties.get();

  utils_ = std::make_shared<Utils>();

  SystemState::Initialize(
#if USE_LVM_STATEFUL_PARTITION
      std::move(mock_lvmd_proxy_wrapper_),
#endif  // USE_LVM_STATEFUL_PARTITION
      std::move(mock_image_loader_proxy_), std::move(mock_update_engine_proxy_),
      std::move(mock_installer_), std::move(mock_session_manager_proxy_),
      &mock_state_change_reporter_, std::move(mock_boot_slot_),
      std::move(mock_metrics), std::move(mock_system_properties),
      manifest_path_, preloaded_content_path_, factory_install_path_,
      deployed_content_path_, content_path_, prefs_path_, users_path_,
      daemon_store_path_, verification_file_path_, &clock_,
      /*for_test=*/true);
  ON_CALL(*mock_installer_ptr_, IsReady()).WillByDefault(Return(true));
#if USE_LVM_STATEFUL_PARTITION
  SystemState::Get()->SetIsLvmStackEnabled(true);
#endif  // USE_LVM_STATEFUL_PARTITION

  ON_CALL(*mock_session_manager_proxy_ptr_, RetrievePrimarySession(_, _, _, _))
      .WillByDefault(DoAll(SetArgPointee<1>("user_hash"), Return(true)));
}

void BaseTest::SetUpFilesAndDirectories() {
  // Initialize DLC path.
  CHECK(scoped_temp_dir_.CreateUniqueTempDir());
  manifest_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "rootfs");
  preloaded_content_path_ =
      JoinPaths(scoped_temp_dir_.GetPath(), "preloaded_stateful");
  factory_install_path_ =
      JoinPaths(scoped_temp_dir_.GetPath(), "factory_install");
  deployed_content_path_ =
      JoinPaths(scoped_temp_dir_.GetPath(), "deployed_stateful");
  content_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "stateful");
  prefs_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "var_lib_dlcservice");
  users_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "users");
  daemon_store_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "daemon_store");
  verification_file_path_ =
      JoinPaths(scoped_temp_dir_.GetPath(), "verification_file");
  mount_path_ = JoinPaths(scoped_temp_dir_.GetPath(), "mount");
  base::FilePath mount_root_path = JoinPaths(mount_path_, "root");
  base::CreateDirectory(manifest_path_);
  base::CreateDirectory(preloaded_content_path_);
  base::CreateDirectory(factory_install_path_);
  base::CreateDirectory(deployed_content_path_);
  base::CreateDirectory(content_path_);
  base::CreateDirectory(prefs_path_);
  base::CreateDirectory(users_path_);
  base::CreateDirectory(daemon_store_path_);
  base::CreateDirectory(mount_root_path);
  testdata_path_ = JoinPaths(getenv("SRC"), "testdata");

  CHECK(base::WriteFile(verification_file_path_, "verification-value"));

  // Create DLC metadata.
  metadata::Metadata test_metadata(manifest_path_);
  ASSERT_TRUE(test_metadata.Initialize());
  for (auto&& id : {kFirstDlc, kSecondDlc, kThirdDlc, kFourthDlc, kScaledDlc,
                    kForceOTADlc, kUserTiedDlc}) {
    SetUpMetadata(id, JoinPaths(testdata_path_, id, kPackage, kManifestName),
                  test_metadata);
    supported_dlc_.emplace(id);
  }
}

int64_t GetFileSize(const base::FilePath& path) {
  int64_t file_size;
  EXPECT_TRUE(base::GetFileSize(path, &file_size));
  return file_size;
}

void BaseTest::SetUpMetadata(const std::string id,
                             const base::FilePath& manifest_path,
                             metadata::Metadata& metadata) {
  std::string manifest_raw;
  ASSERT_TRUE(base::ReadFileToString(manifest_path, &manifest_raw));
  auto manifest_value = base::JSONReader::ReadAndReturnValueWithError(
      manifest_raw, base::JSON_PARSE_RFC);
  ASSERT_TRUE(manifest_value.has_value() && manifest_value->is_dict());
  ASSERT_TRUE(metadata.Set(
      id, metadata::Metadata::Entry{std::move(manifest_value->GetDict()), ""}));
}

base::FilePath BaseTest::SetUpImage(const base::FilePath& root,
                                    const DlcId& id) {
  auto manifest = utils_->GetDlcManifest(id, manifest_path_);
  base::FilePath image_path = JoinPaths(root, id, kPackage, kDlcImageFileName);
  CreateFile(image_path, manifest->size());
  EXPECT_TRUE(base::PathExists(image_path));

  string data(manifest->size(), '1');
  WriteToImage(image_path, data);

  return image_path;
}

base::FilePath BaseTest::SetUpDlcPreloadedImage(const DlcId& id) {
  return SetUpImage(preloaded_content_path_, id);
}

base::FilePath BaseTest::SetUpDlcFactoryImage(const DlcId& id) {
  return SetUpImage(factory_install_path_, id);
}

base::FilePath BaseTest::SetUpDlcDeployedImage(const DlcId& id) {
  return SetUpImage(deployed_content_path_, id);
}

// Will create |path/|id|/|package|/dlc_[a|b]/dlc.img files.
void BaseTest::SetUpDlcWithSlots(const DlcId& id) {
  auto manifest = utils_->GetDlcManifest(id, manifest_path_);
  // Create DLC content sub-directories and empty images.
  for (const auto& slot : {BootSlot::Slot::A, BootSlot::Slot::B}) {
    base::FilePath image_path =
        GetDlcImagePath(content_path_, id, kPackage, slot);
    CreateFile(image_path, manifest->preallocated_size());
    LOG(INFO) << manifest->preallocated_size();
  }
}

void BaseTest::InstallViaInstaller(const vector<string>& ids) {
  for (const auto& id : ids) {
    auto manifest = utils_->GetDlcManifest(id, manifest_path_);
    base::FilePath image_path = GetDlcImagePath(
        content_path_, id, kPackage, SystemState::Get()->active_boot_slot());

    string data(manifest->size(), '1');
    WriteToImage(image_path, data);
  }
}

void BaseTest::SetMountPath(const string& mount_path_expected) {
  ON_CALL(*mock_image_loader_proxy_ptr_, LoadDlcImage(_, _, _, _, _, _))
      .WillByDefault(
          DoAll(SetArgPointee<3>(mount_path_expected), Return(true)));
}

}  // namespace dlcservice
