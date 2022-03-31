// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_VTPM_SERVICE_H_
#define VTPM_VTPM_SERVICE_H_

#include <memory>

#include <base/memory/ref_counted.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_object.h>

#include "vtpm/dbus_interface.h"
#include "vtpm/vtpm_interface.pb.h"

// Requires `vtpm/vtpm_interface.pb.h`
#include "vtpm/dbus_adaptors/org.chromium.Vtpm.h"

namespace vtpm {

class VtpmService : public org::chromium::VtpmInterface {
 public:
  VtpmService() = default;
  VtpmService(const VtpmService&) = delete;
  VtpmService& operator=(const VtpmService&) = delete;

  ~VtpmService() override = default;

  // org::chromium::VtpmInterface overrides.
  void SendCommand(
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<SendCommandResponse>> response,
      const SendCommandRequest& request) override;
};

class VtpmServiceAdaptor : public org::chromium::VtpmAdaptor {
 public:
  explicit VtpmServiceAdaptor(org::chromium::VtpmInterface* vtpm_interface,
                              scoped_refptr<dbus::Bus> bus)
      : org::chromium::VtpmAdaptor(vtpm_interface),
        dbus_object_(nullptr, bus, dbus::ObjectPath(kVtpmServicePath)) {}
  VtpmServiceAdaptor(const VtpmServiceAdaptor&) = delete;
  VtpmServiceAdaptor& operator=(const VtpmServiceAdaptor&) = delete;

  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
    RegisterWithDBusObject(&dbus_object_);
    dbus_object_.RegisterAsync(cb);
  }

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
};

}  // namespace vtpm

#endif  // VTPM_VTPM_SERVICE_H_
