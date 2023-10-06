// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/dbus_adaptor.h"

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

bool DBusAdaptor::GetDebugDumps(brillo::ErrorPtr* error,
                                DebugDumps* out_DebugDumps) {
  auto dumps = manager_->output_manager()->AvailableDumps();
  for (auto dump : dumps) {
    // TODO(b/308984163): Add the metadata information to
    // fbpreprocessor::FirmwareDump instead of hardcoding it here.
    auto debug_dump = out_DebugDumps->add_dump();
    debug_dump->set_type(DebugDump_Type_WIFI);
    auto wifi_dump = debug_dump->mutable_wifi_dump();
    wifi_dump->set_dmpfile(dump.DumpFile().value());
    wifi_dump->set_state(WiFiDump_State_RAW);
    wifi_dump->set_vendor(WiFiDump_Vendor_IWLWIFI);
  }
  return true;
}

}  // namespace fbpreprocessor
