
// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biometrics_manager_record_wrapper.h"
#include "biod/biometrics_manager_wrapper.h"

#include <brillo/dbus/async_event_sequencer.h>
#include <dbus/object_proxy.h>

namespace biod {

using brillo::dbus_utils::DBusInterface;
using brillo::dbus_utils::ExportedObjectManager;
using dbus::ObjectPath;

RecordWrapper::RecordWrapper(
    BiometricsManagerWrapper* biometrics_manager,
    std::unique_ptr<BiometricsManagerRecordInterface> record,
    ExportedObjectManager* object_manager,
    const ObjectPath& object_path)
    : biometrics_manager_(biometrics_manager),
      record_(std::move(record)),
      dbus_object_(object_manager, object_manager->GetBus(), object_path),
      object_path_(object_path) {
  DBusInterface* record_interface =
      dbus_object_.AddOrGetInterface(kRecordInterface);
  property_label_.SetValue(record_->GetLabel());
  record_interface->AddProperty(kRecordLabelProperty, &property_label_);
  record_interface->AddSimpleMethodHandlerWithError(
      kRecordSetLabelMethod,
      base::BindRepeating(&RecordWrapper::SetLabel, base::Unretained(this)));
  record_interface->AddSimpleMethodHandlerWithError(
      kRecordRemoveMethod,
      base::BindRepeating(&RecordWrapper::Remove, base::Unretained(this)));
  dbus_object_.RegisterAndBlock();
}

RecordWrapper::~RecordWrapper() {
  dbus_object_.UnregisterAndBlock();
}

bool RecordWrapper::SetLabel(brillo::ErrorPtr* error,
                             const std::string& new_label) {
  if (!record_->SetLabel(new_label)) {
    *error = brillo::Error::Create(FROM_HERE, kDomain, kInternalError,
                                   "Failed to set label");
    return false;
  }
  property_label_.SetValue(new_label);
  return true;
}

bool RecordWrapper::Remove(brillo::ErrorPtr* error) {
  if (!record_->Remove()) {
    *error = brillo::Error::Create(FROM_HERE, kDomain, kInternalError,
                                   "Failed to remove record");
    return false;
  }
  biometrics_manager_->RefreshRecordObjects();
  return true;
}

}  // namespace biod
