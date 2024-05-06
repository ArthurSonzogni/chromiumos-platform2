// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/bluetooth_tool.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/cryptohome.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <linux/capability.h>

#include "debugd/src/sandboxed_process.h"

namespace debugd {

namespace {

constexpr char kBtmonSeccompPath[] = "/usr/share/policy/btmon-seccomp.policy";
constexpr uint64_t kBtmonCapabilities = CAP_TO_MASK(CAP_NET_RAW);
constexpr char kBtmonLogName[] = "capture.btsnoop";
constexpr char kBtmonExecPath[] = "/usr/bin/btmon";

}  // namespace

bool BluetoothTool::StartBtsnoop() {
  std::string obfuscated_name;

  if (!GetCurrentUserObfuscatedName(&obfuscated_name)) {
    return false;
  }

  // If for whatever reason there's already a running sandboxed btmon, just
  // stop it first.
  StopBtsnoop();
  return StartSandboxedBtsnoop(obfuscated_name);
}

void BluetoothTool::StopBtsnoop() {
  if (btmon_) {
    btmon_->KillProcessGroup();
    btmon_ = nullptr;
  }
}

bool BluetoothTool::IsBtsnoopRunning() {
  return btmon_ != nullptr;
}

std::unique_ptr<SandboxedProcess> BluetoothTool::CreateSandboxedProcess() {
  return std::make_unique<SandboxedProcess>();
}

// This method is blocking.
bool BluetoothTool::GetCurrentUserObfuscatedName(std::string* out_name) {
  // We send a DBus message to login_manager's RetrievePrimarySession to get the
  // current user's obfuscated name.
  dbus::ObjectProxy* proxy = bus_->GetObjectProxy(
      login_manager::kSessionManagerServiceName,
      dbus::ObjectPath(login_manager::kSessionManagerServicePath));
  if (proxy == nullptr) {
    LOG(ERROR) << "Failed to obtain SessionManager D-Bus proxy";
    return false;
  }

  dbus::MethodCall method_call(
      login_manager::kSessionManagerInterface,
      login_manager::kSessionManagerRetrievePrimarySession);
  base::expected<std::unique_ptr<dbus::Response>, dbus::Error> method_result =
      proxy->CallMethodAndBlock(&method_call,
                                dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!method_result.has_value() || !method_result.value()) {
    LOG(ERROR) << "Failed to call RetrievePrimarySession";
    return false;
  }

  dbus::MessageReader reader(method_result.value().get());
  std::string user_name;
  std::string obfuscated_name;
  if (!reader.PopString(&user_name) || !reader.PopString(&obfuscated_name)) {
    LOG(ERROR) << "Failed to parse DBus message";
    return false;
  }

  // If it's empty, no user is signed in.
  if (obfuscated_name.empty()) {
    return false;
  }

  *out_name = obfuscated_name;
  return true;
}

bool BluetoothTool::StartSandboxedBtsnoop(const std::string& obfuscated_name) {
  // Allow write access to the daemon-store.
  base::FilePath daemon_store_path =
      brillo::cryptohome::home::GetDaemonStorePath(
          brillo::cryptohome::home::ObfuscatedUsername(obfuscated_name),
          "debugd");
  std::string bind_option =
      std::format("--bind-mount={},,1", daemon_store_path.value());

  const std::vector<std::string> minijail_args{
      "-l",
      "-N",
      "-p",
      "--uts",  // Enter ipc, cgroup, pid, and uts namespaces.
      "--profile=minimalistic-mountns-nodev",  // Minimal mount namespace.
      "-n",                                    // Set no_new_privs.
      "-i",  // Run the program in the background.
      "--mount=/run,/run,tmpfs,0xe,mode=755,size=10M",  // Mount tmpfs at /run.
      bind_option,
  };

  btmon_ = CreateSandboxedProcess();
  btmon_->SandboxAs(SandboxedProcess::kDefaultUser,
                    SandboxedProcess::kDefaultGroup);
  btmon_->SetCapabilities(kBtmonCapabilities);
  btmon_->SetSeccompFilterPolicyFile(kBtmonSeccompPath);

  if (!btmon_->Init(minijail_args)) {
    LOG(ERROR) << "Failed to initialize Btmon object";
    btmon_ = nullptr;
    return false;
  }

  // The btmon writes to a max of 2 logs which are rotated when the size limit
  // is reached.
  base::FilePath file_path = daemon_store_path.Append(kBtmonLogName);
  btmon_->AddArg(kBtmonExecPath);
  btmon_->AddArg("-S");  // Capture SCO data.
  btmon_->AddArg("-0");  // Zero out privacy data.
  btmon_->AddArg("-f");  // Enable log rotation.
  btmon_->AddArg("-l");  // Set size limit to 2.5 MB.
  btmon_->AddArg("2500000");
  btmon_->AddArg("-w");  // Write to file_path.
  btmon_->AddArg(file_path.value());
  if (!btmon_->Start()) {
    LOG(ERROR) << "Failed to start Btmon process";
    btmon_ = nullptr;
    return false;
  }

  return true;
}

bool BluetoothTool::CopyBtsnoop(const base::ScopedFD& fd) {
  std::string obfuscated_name;

  if (!GetCurrentUserObfuscatedName(&obfuscated_name)) {
    return false;
  }

  base::FilePath daemon_store_path =
      brillo::cryptohome::home::GetDaemonStorePath(
          brillo::cryptohome::home::ObfuscatedUsername(obfuscated_name),
          "debugd");
  base::FilePath file_path = daemon_store_path.Append(kBtmonLogName);

  std::optional<std::vector<uint8_t>> content =
      base::ReadFileToBytes(file_path);
  if (!content) {
    LOG(ERROR) << "Failed to read btsnoop log";
    return false;
  }
  if (!base::WriteFileDescriptor(fd.get(), content.value())) {
    LOG(ERROR) << "Failed to write btsnoop copy";
    return false;
  }

  return true;
}

void BluetoothTool::OnSessionStarted() {
  // Nothing to do here.
}

void BluetoothTool::OnSessionStopped() {
  // When the old session stops, we need to stop logging, otherwise we might
  // record a new user's bluetooth traffic as the old user's.
  StopBtsnoop();
}

}  // namespace debugd
