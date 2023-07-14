// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/stringprintf.h"
#include "biod/biometrics_manager_record.h"
#include "biod/biometrics_manager_record_wrapper.h"
#include "biod/biometrics_manager_wrapper.h"
#include "biod/mock_biod_metrics.h"
#include "biod/mock_biometrics_manager.h"
#include "biod/mock_biometrics_manager_record.h"
#include "biod/mock_cros_fp_biometrics_manager.h"
#include "biod/mock_cros_fp_device.h"
#include "biod/mock_cros_fp_record_manager.h"
#include "biod/mock_session_state_manager.h"
#include "dbus/mock_bus.h"
#include "dbus/mock_exported_object.h"
#include "dbus/mock_object_proxy.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/environment.h>
#include <brillo/dbus/mock_exported_object_manager.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>

using testing::_;
using testing::IsEmpty;
using testing::Return;
using testing::SaveArg;
using testing::StrEq;

using dbus::ObjectPath;

namespace biod {
namespace {

using brillo::dbus_utils::DBusInterface;
using brillo::dbus_utils::ExportedObjectManager;
using dbus::ObjectPath;

class MockBiometricsManagerRecordWrapper
    : public BiometricsManagerRecordWrapper {
 public:
  MockBiometricsManagerRecordWrapper(
      BiometricsManagerWrapper* biometrics_manager,
      std::unique_ptr<BiometricsManagerRecordInterface> record,
      ExportedObjectManager* object_manager,
      const ObjectPath& object_path)
      : BiometricsManagerRecordWrapper(biometrics_manager,
                                       std::move(record),
                                       object_manager,
                                       object_path) {}
  ~MockBiometricsManagerRecordWrapper() = default;

  bool SetLabel(brillo::ErrorPtr* error, const std::string& new_label) {
    return BiometricsManagerRecordWrapper::SetLabel(error, new_label);
  }

  bool Remove(brillo::ErrorPtr* error) {
    return BiometricsManagerRecordWrapper::Remove(error);
  }

  std::string GetPropertyLabelValue() const { return property_label_.value(); }
};

class BiometricsManagerRecordWrapperTest : public ::testing::Test {
 public:
  BiometricsManagerRecordWrapperTest() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);
    ON_CALL(*bus_, GetExportedObject)
        .WillByDefault(Invoke(
            this, &BiometricsManagerRecordWrapperTest::GetExportedObject));

    proxy_ =
        new dbus::MockObjectProxy(bus_.get(), dbus::kDBusServiceName,
                                  dbus::ObjectPath(dbus::kDBusServicePath));

    ON_CALL(*bus_, GetObjectProxy(dbus::kDBusServiceName, _))
        .WillByDefault(Return(proxy_.get()));

    auto mock_biometrics_manager = std::make_unique<MockBiometricsManager>();
    bio_manager_ = mock_biometrics_manager.get();

    object_manager_ =
        std::make_unique<brillo::dbus_utils::MockExportedObjectManager>(
            bus_, dbus::ObjectPath(kBiodServicePath));
    session_manager_ = std::make_unique<MockSessionStateManager>();

    EXPECT_CALL(*session_manager_, AddObserver).Times(1);

    mock_bio_path_ = dbus::ObjectPath(
        base::StringPrintf("%s/%s", kBiodServicePath, "MockBiometricsManager"));

    auto sequencer =
        base::MakeRefCounted<brillo::dbus_utils::AsyncEventSequencer>();

    wrapper_.emplace(
        std::move(mock_biometrics_manager), object_manager_.get(),
        session_manager_.get(), mock_bio_path_,
        sequencer->GetHandler("Failed to register BiometricsManager", false));
  }

 protected:
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> proxy_;
  MockBiometricsManager* bio_manager_;
  std::unique_ptr<brillo::dbus_utils::MockExportedObjectManager>
      object_manager_;
  dbus::ObjectPath mock_bio_path_;
  std::map<std::string, scoped_refptr<dbus::MockExportedObject>>
      exported_objects_;
  std::unique_ptr<MockSessionStateManager> session_manager_;
  std::optional<BiometricsManagerWrapper> wrapper_;

 private:
  std::map<std::string, dbus::ExportedObject::MethodCallCallback>
      method_callbacks_;

  dbus::ExportedObject* GetExportedObject(const dbus::ObjectPath& object_path);

  bool ExportMethodAndBlock(
      const std::string& interface_name,
      const std::string& method_name,
      const dbus::ExportedObject::MethodCallCallback& method_call_callback);
};

dbus::ExportedObject* BiometricsManagerRecordWrapperTest::GetExportedObject(
    const dbus::ObjectPath& object_path) {
  auto iter = exported_objects_.find(object_path.value());
  if (iter != exported_objects_.end()) {
    return iter->second.get();
  }

  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus_.get(), object_path);
  exported_objects_[object_path.value()] = exported_object;

  ON_CALL(*exported_object, ExportMethodAndBlock)
      .WillByDefault(Invoke(
          this, &BiometricsManagerRecordWrapperTest::ExportMethodAndBlock));

  return exported_object.get();
}

bool BiometricsManagerRecordWrapperTest::ExportMethodAndBlock(
    const std::string& interface_name,
    const std::string& method_name,
    const dbus::ExportedObject::MethodCallCallback& method_call_callback) {
  std::string full_name = interface_name + "." + method_name;
  method_callbacks_[full_name] = method_call_callback;

  return true;
}

TEST_F(BiometricsManagerRecordWrapperTest, TestSetLabelTrue) {
  auto mock_record = std::make_unique<MockBiometricsManagerRecord>();

  std::string records_root_path =
      mock_bio_path_.value() + std::string("/Record");

  const char kRecordId1[] = "00000000_0000_0000_0000_000000000001";
  std::string record_id = kRecordId1;
  ObjectPath record_path(records_root_path + kRecordId1);

  EXPECT_CALL(*mock_record, SetLabel(_)).WillOnce(Return(true));
  auto biometrics_manager_record_wrapper = MockBiometricsManagerRecordWrapper(
      &wrapper_.value(), std::move(mock_record), object_manager_.get(),
      record_path);

  const std::string label = "record_label";
  brillo::ErrorPtr error;

  EXPECT_TRUE(biometrics_manager_record_wrapper.SetLabel(&error, label));

  EXPECT_THAT(biometrics_manager_record_wrapper.GetPropertyLabelValue(),
              StrEq(label));
}

TEST_F(BiometricsManagerRecordWrapperTest, TestSetLabelFalse) {
  auto mock_record = std::make_unique<MockBiometricsManagerRecord>();

  std::string records_root_path =
      mock_bio_path_.value() + std::string("/Record");

  const char kRecordId1[] = "00000000_0000_0000_0000_000000000001";
  std::string record_id = kRecordId1;
  ObjectPath record_path(records_root_path + kRecordId1);

  EXPECT_CALL(*mock_record, SetLabel(_)).WillOnce(Return(false));
  auto biometrics_manager_record_wrapper = MockBiometricsManagerRecordWrapper(
      &wrapper_.value(), std::move(mock_record), object_manager_.get(),
      record_path);

  const std::string label = "record_label";
  brillo::ErrorPtr error;

  EXPECT_FALSE(biometrics_manager_record_wrapper.SetLabel(&error, label));
  EXPECT_THAT(error->GetMessage(), StrEq("Failed to set label"));

  EXPECT_THAT(biometrics_manager_record_wrapper.GetPropertyLabelValue(),
              IsEmpty());
}

TEST_F(BiometricsManagerRecordWrapperTest, TestRemoveTrue) {
  auto mock_record = std::make_unique<MockBiometricsManagerRecord>();

  std::string records_root_path =
      mock_bio_path_.value() + std::string("/Record");

  const char kRecordId1[] = "00000000_0000_0000_0000_000000000001";
  std::string record_id = kRecordId1;
  ObjectPath record_path(records_root_path + kRecordId1);

  EXPECT_CALL(*mock_record, Remove()).WillOnce(Return(true));
  auto biometrics_manager_record_wrapper = MockBiometricsManagerRecordWrapper(
      &wrapper_.value(), std::move(mock_record), object_manager_.get(),
      record_path);

  const std::string label = "record_label";
  brillo::ErrorPtr error;

  EXPECT_TRUE(biometrics_manager_record_wrapper.Remove(&error));
}

TEST_F(BiometricsManagerRecordWrapperTest, TestRemoveFalse) {
  auto mock_record = std::make_unique<MockBiometricsManagerRecord>();

  std::string records_root_path =
      mock_bio_path_.value() + std::string("/Record");

  const char kRecordId1[] = "00000000_0000_0000_0000_000000000001";
  std::string record_id = kRecordId1;
  ObjectPath record_path(records_root_path + kRecordId1);

  EXPECT_CALL(*mock_record, Remove()).WillOnce(Return(false));
  auto biometrics_manager_record_wrapper = MockBiometricsManagerRecordWrapper(
      &wrapper_.value(), std::move(mock_record), object_manager_.get(),
      record_path);

  const std::string label = "record_label";
  brillo::ErrorPtr error;

  EXPECT_FALSE(biometrics_manager_record_wrapper.Remove(&error));
  EXPECT_THAT(error->GetMessage(), StrEq("Failed to remove record"));
}

}  // namespace
}  // namespace biod
