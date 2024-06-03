// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/crash_reporter_dbus_adaptor.h"

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>

#include "fbpreprocessor/constants.h"
#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/input_manager.h"

namespace {
constexpr char kCrashReporterServiceName[] = "org.chromium.CrashReporter";
constexpr char kCrashReporterServicePath[] = "/org/chromium/CrashReporter";
constexpr char kCrashReporterInterface[] =
    "org.chromium.CrashReporterInterface";
constexpr char kCrashReporterFirmwareDumpCreated[] = "DebugDumpCreated";
}  // namespace

namespace fbpreprocessor {

CrashReporterDBusAdaptor::CrashReporterDBusAdaptor(Manager* manager,
                                                   dbus::Bus* bus)
    : manager_(manager) {
  crash_reporter_proxy_ = bus->GetObjectProxy(
      kCrashReporterServiceName, dbus::ObjectPath(kCrashReporterServicePath));

  crash_reporter_proxy_->ConnectToSignal(
      kCrashReporterInterface, kCrashReporterFirmwareDumpCreated,
      base::BindRepeating(&CrashReporterDBusAdaptor::OnFirmwareDumpCreated,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&CrashReporterDBusAdaptor::OnSignalConnected,
                     weak_factory_.GetWeakPtr()));
}

void CrashReporterDBusAdaptor::OnFirmwareDumpCreated(
    dbus::Signal* signal) const {
  VLOG(kLocalDebugVerbosity) << __func__;
  CHECK(signal != nullptr) << "Invalid " << __func__ << " signal.";
  dbus::MessageReader signal_reader(signal);
  DebugDumps dumps;

  if (!signal_reader.PopArrayOfBytesAsProto(&dumps)) {
    LOG(ERROR) << "Failed to parse " << kCrashReporterFirmwareDumpCreated
               << " signal.";
    return;
  }
  for (auto dump : dumps.dump()) {
    if (dump.has_wifi_dump()) {
      base::FilePath path(dump.wifi_dump().dmpfile());
      FirmwareDump fw_dump(path, FirmwareDump::Type::kWiFi);
      LOG(INFO) << __func__ << ": New WiFi dump file detected.";
      VLOG(kLocalOnlyDebugVerbosity) << "Detected new file " << fw_dump << ".";
      manager_->input_manager()->OnNewFirmwareDump(fw_dump);
    } else if (dump.has_bluetooth_dump()) {
      base::FilePath path(dump.bluetooth_dump().dmpfile());
      FirmwareDump fw_dump(path, FirmwareDump::Type::kBluetooth);
      LOG(INFO) << __func__ << ": New Bluetooth dump file detected.";
      VLOG(kLocalOnlyDebugVerbosity) << "Detected new file " << fw_dump << ".";
      manager_->input_manager()->OnNewFirmwareDump(fw_dump);
    }
  }
}

void CrashReporterDBusAdaptor::OnSignalConnected(
    const std::string& interface_name,
    const std::string& signal_name,
    bool success) const {
  if (!success)
    LOG(ERROR) << "Failed to connect to signal " << signal_name
               << " of interface " << interface_name;
  if (success) {
    LOG(INFO) << "Connected to signal " << signal_name << " of interface "
              << interface_name;
  }
}

}  // namespace fbpreprocessor
