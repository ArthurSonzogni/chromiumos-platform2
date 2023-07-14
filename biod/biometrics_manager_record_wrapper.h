// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOMETRICS_MANAGER_RECORD_WRAPPER_H_
#define BIOD_BIOMETRICS_MANAGER_RECORD_WRAPPER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <brillo/dbus/exported_object_manager.h>
#include <dbus/message.h>
#include <dbus/object_path.h>

#include "biod/biometrics_manager_record_interface.h"

namespace biod {

class BiometricsManagerWrapper;

class BiometricsManagerRecordWrapper {
 public:
  BiometricsManagerRecordWrapper(
      BiometricsManagerWrapper* biometrics_manager,
      std::unique_ptr<BiometricsManagerRecordInterface> record,
      brillo::dbus_utils::ExportedObjectManager* object_manager,
      const dbus::ObjectPath& object_path);
  BiometricsManagerRecordWrapper(const BiometricsManagerRecordWrapper&) =
      delete;
  BiometricsManagerRecordWrapper& operator=(
      const BiometricsManagerRecordWrapper&) = delete;

  ~BiometricsManagerRecordWrapper();

  const dbus::ObjectPath& path() const { return object_path_; }

  std::string GetUserId() const { return record_->GetUserId(); }

 private:
  BiometricsManagerWrapper* biometrics_manager_;
  std::unique_ptr<BiometricsManagerRecordInterface> record_;
  brillo::dbus_utils::DBusObject dbus_object_;
  dbus::ObjectPath object_path_;

 protected:
  bool SetLabel(brillo::ErrorPtr* error, const std::string& new_label);
  bool Remove(brillo::ErrorPtr* error);

  brillo::dbus_utils::ExportedProperty<std::string> property_label_;
};

}  // namespace biod

#endif  // BIOD_BIOMETRICS_MANAGER_RECORD_WRAPPER_H_
