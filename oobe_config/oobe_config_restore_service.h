// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_OOBE_CONFIG_RESTORE_SERVICE_H_
#define OOBE_CONFIG_OOBE_CONFIG_RESTORE_SERVICE_H_

#include <memory>
#include <utility>
#include <vector>

#include <brillo/dbus/async_event_sequencer.h>
#include <dbus_adaptors/org.chromium.OobeConfigRestore.h>
#include <power_manager-client/power_manager/dbus-proxies.h>

namespace oobe_config {

// Used as buffer for serialized protobufs.
using ProtoBlob = std::vector<uint8_t>;

// Implementation of OobeConfigRestore D-Bus interface.
class OobeConfigRestoreService
    : public org::chromium::OobeConfigRestoreAdaptor,
      public org::chromium::OobeConfigRestoreInterface {
 public:
  explicit OobeConfigRestoreService(
      std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
      bool allow_unencrypted,
      bool skip_reboot_for_testing);
  ~OobeConfigRestoreService() override;

  // Registers the D-Bus object and interfaces.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
          completion_callback);

  // org::chromium::OobeConfigRestoreInterface
  //   - See org.chromium.OobeConfigRestoreInterface.xml
  void ProcessAndGetOobeAutoConfig(int32_t* error,
                                   ProtoBlob* oobe_config_blob) override;

 private:
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  std::unique_ptr<org::chromium::PowerManagerProxy> power_manager_proxy_;
  bool allow_unencrypted_;
  bool skip_reboot_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(OobeConfigRestoreService);
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_OOBE_CONFIG_RESTORE_SERVICE_H_
