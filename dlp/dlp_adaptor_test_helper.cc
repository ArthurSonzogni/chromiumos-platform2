// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_adaptor_test_helper.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <brillo/dbus/dbus_object.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlp/dbus-constants.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/object_path.h>
#include <featured/fake_platform_features.h>
#include <gtest/gtest.h>

#include "dlp/dlp_adaptor.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace dlp {

namespace {
constexpr char kObjectPath[] = "/object/path";
}  // namespace

DlpAdaptorTestHelper::DlpAdaptorTestHelper() {
  const dbus::ObjectPath object_path(kObjectPath);
  bus_ = base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options());

  // Mock out D-Bus initialization.
  mock_exported_object_ =
      base::MakeRefCounted<dbus::MockExportedObject>(bus_.get(), object_path);

  EXPECT_CALL(*bus_, GetExportedObject(_))
      .WillRepeatedly(Return(mock_exported_object_.get()));

  EXPECT_CALL(*bus_, HasDBusThread()).WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
      .Times(testing::AnyNumber());

  mock_dlp_files_policy_service_proxy_ =
      base::MakeRefCounted<dbus::MockObjectProxy>(
          bus_.get(), dlp::kDlpFilesPolicyServiceName,
          dbus::ObjectPath(dlp::kDlpFilesPolicyServicePath));
  EXPECT_CALL(*bus_, GetObjectProxy(dlp::kDlpFilesPolicyServiceName, _))
      .WillRepeatedly(Return(mock_dlp_files_policy_service_proxy_.get()));

  mock_session_manager_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
      bus_.get(), login_manager::kSessionManagerServiceName,
      dbus::ObjectPath(login_manager::kSessionManagerServicePath));
  EXPECT_CALL(*bus_,
              GetObjectProxy(login_manager::kSessionManagerServiceName, _))
      .WillRepeatedly(Return(mock_session_manager_proxy_.get()));

  EXPECT_TRUE(home_dir_.CreateUniqueTempDir());

  base::ScopedFD fd_1, fd_2;
  EXPECT_TRUE(base::CreatePipe(&fd_1, &fd_2));

  feature_lib_ = std::make_unique<feature::FakePlatformFeatures>(bus_);
  CHECK_NE(feature_lib_.get(), nullptr);
  adaptor_ = std::make_unique<DlpAdaptor>(
      std::make_unique<brillo::dbus_utils::DBusObject>(nullptr, bus_,
                                                       object_path),
      feature_lib_.get(), fd_1.release(), fd_2.release(), home_dir_.GetPath());
  std::unique_ptr<FakeMetricsLibrary> metrics_library =
      std::make_unique<FakeMetricsLibrary>();
  metrics_library_ = metrics_library.get();
  adaptor_->SetMetricsLibraryForTesting(std::move(metrics_library));
}

DlpAdaptorTestHelper::~DlpAdaptorTestHelper() = default;

void DlpAdaptorTestHelper::ProcessFileOpenRequest(
    ino_t inode, int pid, base::OnceCallback<void(bool)> callback) {
  adaptor_->ProcessFileOpenRequest(inode, pid, std::move(callback));
}

void DlpAdaptorTestHelper::OnFileDeleted(ino_t inode) {
  adaptor_->OnFileDeleted(inode);
}

ino_t DlpAdaptorTestHelper::GetInodeValue(const std::string& path) {
  return DlpAdaptor::GetInodeValue(path);
}

void DlpAdaptorTestHelper::ReCreateAdaptor() {
  ASSERT_NE(adaptor_.get(), nullptr);
  adaptor_.reset();
  metrics_library_ = nullptr;

  EXPECT_TRUE(home_dir_.Delete());
  EXPECT_TRUE(home_dir_.CreateUniqueTempDir());

  base::ScopedFD fd_1, fd_2;
  EXPECT_TRUE(base::CreatePipe(&fd_1, &fd_2));

  adaptor_ = std::make_unique<DlpAdaptor>(
      std::make_unique<brillo::dbus_utils::DBusObject>(
          nullptr, bus_, dbus::ObjectPath(kObjectPath)),
      feature_lib_.get(), fd_1.release(), fd_2.release(), home_dir_.GetPath());
  std::unique_ptr<FakeMetricsLibrary> metrics_library =
      std::make_unique<FakeMetricsLibrary>();
  metrics_library_ = metrics_library.get();
  adaptor_->SetMetricsLibraryForTesting(std::move(metrics_library));
}

std::vector<int> DlpAdaptorTestHelper::GetMetrics(
    const std::string& metrics_name) const {
  if (metrics_library_) {
    return metrics_library_->GetCalls(metrics_name);
  }
  return {};
}

}  // namespace dlp
