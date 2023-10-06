// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_DBUS_ADAPTOR_H_
#define FBPREPROCESSOR_DBUS_ADAPTOR_H_

#include <utility>

#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>

#include <fbpreprocessor/dbus_adaptors/org.chromium.FbPreprocessor.h>

#include "fbpreprocessor/manager.h"

namespace fbpreprocessor {

class DBusAdaptor : public org::chromium::FbPreprocessorInterface,
                    public org::chromium::FbPreprocessorAdaptor {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus, Manager* manager);
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
    RegisterWithDBusObject(&dbus_object_);
    dbus_object_.RegisterAsync(std::move(cb));
  }

  bool GetDebugDumps(brillo::ErrorPtr* error,
                     DebugDumps* out_DebugDumps) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;

  Manager* manager_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_DBUS_ADAPTOR_H_
