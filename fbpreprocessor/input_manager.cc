// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/input_manager.h"

#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <brillo/files/file_util.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>

#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/pseudonymization_manager.h"
#include "fbpreprocessor/session_state_manager.h"
#include "fbpreprocessor/storage.h"

namespace {
constexpr char kCrashReporterServiceName[] = "org.chromium.CrashReporter";
constexpr char kCrashReporterServicePath[] = "/org/chromium/CrashReporter";
constexpr char kCrashReporterInterface[] =
    "org.chromium.CrashReporterInterface";
constexpr char kCrashReporterFirmwareDumpCreated[] = "DebugDumpCreated";
}  // namespace

namespace fbpreprocessor {

InputManager::InputManager(Manager* manager, dbus::Bus* bus)
    : manager_(manager) {
  manager_->session_state_manager()->AddObserver(this);
  crash_reporter_proxy_ = bus->GetObjectProxy(
      kCrashReporterServiceName, dbus::ObjectPath(kCrashReporterServicePath));

  crash_reporter_proxy_->ConnectToSignal(
      kCrashReporterInterface, kCrashReporterFirmwareDumpCreated,
      base::BindRepeating(&InputManager::OnFirmwareDumpCreated,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&InputManager::OnSignalConnected,
                     weak_factory_.GetWeakPtr()));
}

InputManager::~InputManager() {
  if (manager_->session_state_manager())
    manager_->session_state_manager()->RemoveObserver(this);
}

void InputManager::OnSignalConnected(const std::string& interface_name,
                                     const std::string& signal_name,
                                     bool success) {
  if (!success)
    LOG(ERROR) << "Failed to connect to signal " << signal_name
               << " of interface " << interface_name;
  if (success) {
    LOG(INFO) << "Connected to signal " << signal_name << " of interface "
              << interface_name;
  }
}

void InputManager::OnFirmwareDumpCreated(dbus::Signal* signal) {
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
      FirmwareDump fw_dump(path.RemoveExtension());
      // TODO(b/307593542): remove filenames from logs.
      LOG(INFO) << "Detected new file " << fw_dump << ".";

      if (!manager_->FirmwareDumpsAllowed()) {
        // The feature is disabled, but firmware dumps were created anyway.
        // Delete those firmware dumps.
        LOG(INFO) << "Feature disabled, deleting firmware dump.";
        if (!fw_dump.Delete()) {
          LOG(ERROR) << "Failed to delete firmware dump.";
        }
        continue;
      }
      OnNewFirmwareDump(fw_dump);
    }
  }
}

void InputManager::OnUserLoggedIn(const std::string& user_dir) {
  LOG(INFO) << "User logged in.";
  user_root_dir_.clear();
  if (user_dir.empty()) {
    LOG(ERROR) << "No user directory defined.";
    return;
  }
  user_root_dir_ = base::FilePath(kDaemonStorageRoot).Append(user_dir);
  DeleteAllFiles();
}

void InputManager::OnUserLoggedOut() {
  LOG(INFO) << "User logged out.";
  user_root_dir_.clear();
}

void InputManager::OnNewFirmwareDump(const FirmwareDump& fw_dump) {
  manager_->pseudonymization_manager()->StartPseudonymization(fw_dump);
}

void InputManager::DeleteAllFiles() {
  base::FileEnumerator files(user_root_dir_.Append(kInputDirectory),
                             false /* recursive */,
                             base::FileEnumerator::FILES);
  files.ForEach([](const base::FilePath& path) {
    // TODO(b/307593542): remove filenames from logs.
    LOG(INFO) << "Cleaning up file " << path.BaseName();
    if (!brillo::DeleteFile(path)) {
      // TODO(b/307593542): remove filenames from logs.
      LOG(ERROR) << "Failed to delete " << path.BaseName();
    }
  });
}

}  // namespace fbpreprocessor
