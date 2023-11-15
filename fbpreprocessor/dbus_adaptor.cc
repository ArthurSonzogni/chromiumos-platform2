// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/dbus_adaptor.h"

#include <brillo/dbus/dbus_method_response.h>
#include <dbus/bus.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <fbpreprocessor-client/fbpreprocessor/dbus-constants.h>

#include <fbpreprocessor/dbus_adaptors/org.chromium.FbPreprocessor.h>

#include "fbpreprocessor/output_manager.h"

namespace fbpreprocessor {

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus, Manager* manager)
    : org::chromium::FbPreprocessorAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kFbPreprocessorServicePath)),
      manager_(manager) {}

void DBusAdaptor::GetDebugDumps(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<DebugDumps>>
        response) const {
  manager_->output_manager()->GetDebugDumps(std::move(response));
}

}  // namespace fbpreprocessor
