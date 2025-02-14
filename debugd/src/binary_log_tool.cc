// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/binary_log_tool.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/memory/scoped_refptr.h>
#include <base/logging.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/fbpreprocessor/dbus-constants.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/debugd/dbus-constants.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <fbpreprocessor-client/fbpreprocessor/dbus-proxies.h>
#include <user_data_auth-client/user_data_auth/dbus-proxies.h>

#include "debugd/src/sandboxed_process.h"

namespace {

constexpr char kWiFiTarballName[] = "wifi_fw_dumps.tar.zst";
constexpr char kBluetoothTarballName[] = "bluetooth_fw_dumps.tar.zst";

std::optional<base::FilePath> GetTarballName(
    debugd::FeedbackBinaryLogType type) {
  switch (type) {
    case debugd::FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP:
      return base::FilePath(kWiFiTarballName);
    case debugd::FeedbackBinaryLogType::BLUETOOTH_FIRMWARE_DUMP:
      return base::FilePath(kBluetoothTarballName);
    default:
      return std::nullopt;
  }
}

bool ValidateDirectoryNames(const std::set<base::FilePath>& files,
                            const base::FilePath& daemon_store_path) {
  if (files.empty()) {
    LOG(ERROR) << "No input files";
    return false;
  }

  // The input files, i.e. the dumps processed by fbpreprocessord are stored
  // in "/run/daemon-store/fbpreprocessord/<user_hash>/processed_dumps/".
  base::FilePath processed_dir_path =
      daemon_store_path.Append(fbpreprocessor::kProcessedDirectory);

  for (auto file : files) {
    if (file.DirName() != processed_dir_path) {
      LOG(ERROR) << "Invalid input file path: " << file;
      return false;
    }
  }

  return true;
}

bool CompressFiles(const base::FilePath& outfile,
                   const std::set<base::FilePath>& files,
                   const base::FilePath& base_dir,
                   bool use_minijail) {
  if (files.empty()) {
    LOG(ERROR) << "No input files";
    return false;
  }

  debugd::SandboxedProcess p;
  p.InheritUsergroups();
  p.AllowAccessRootMountNamespace();

  if (use_minijail) {
    std::string args(base_dir.value());
    args.append(",");
    args.append(base_dir.value());
    args.append(",none,MS_BIND|MS_REC");

    std::vector<std::string> minijail_args;
    minijail_args.push_back("-k");
    minijail_args.push_back(args);
    p.Init(minijail_args);
  }

  p.AddArg("/bin/tar");
  p.AddArg("-I zstd");
  p.AddArg("-cf");
  p.AddArg(outfile.value());
  p.AddArg("-C");
  p.AddArg(files.cbegin()->DirName().value());

  for (auto file : files) {
    p.AddArg(file.BaseName().value());
  }

  int ret = p.Run();
  if (ret) {
    PLOG(ERROR) << "Failed to run tar";
  }

  return ret == EXIT_SUCCESS;
}

// Compress the files in |files| to tarball with ZSTD compression and copy the
// contents of the tarball to the |out_fd|. The tarball is deleted once it is
// copied to the FD.
bool CompressAndSendFilesToFD(const base::FilePath& tarball_name,
                              const std::set<base::FilePath>& files,
                              const base::FilePath& daemon_store_path,
                              bool use_minijail,
                              const int out_fd) {
  if (files.empty()) {
    LOG(ERROR) << "No input files";
    return false;
  }

  // The processed dumps are stored in the following directory:
  // "/run/daemon-store/fbpreprocessord/<user_hash>/processed_dumps/",
  // whereas, the intermediate compressed dumps should be stored under:
  // "/run/daemon-store/fbpreprocessord/<user_hash>/scratch/" directory.
  base::FilePath output_dir =
      daemon_store_path.Append(fbpreprocessor::kScratchDirectory);

  if (!base::DirectoryExists(output_dir)) {
    LOG(ERROR) << "Output dir " << output_dir << " doesn't exist";
    return false;
  }

  base::FilePath tarball_path = output_dir.Append(tarball_name);

  if (!CompressFiles(tarball_path, files, daemon_store_path, use_minijail)) {
    LOG(ERROR) << "Failed to compress binary logs";
    return false;
  }

  VLOG(1) << "Attaching debug dumps at " << tarball_path;

  base::File tarfile(tarball_path, base::File::FLAG_OPEN |
                                       base::File::FLAG_READ |
                                       base::File::FLAG_DELETE_ON_CLOSE);
  if (!tarfile.IsValid()) {
    LOG(ERROR) << "Error opening file " << tarball_path << ": "
               << base::File::ErrorToString(tarfile.error_details());
    return false;
  }

  // The out_fd is closed by the caller function. So, use dup() to create a
  // base::File object so that it can be closed independently after the copy
  // operation without interfering with the out_fd.
  int dup_fd = dup(out_fd);
  if (dup_fd == -1) {
    PLOG(ERROR) << "Failed to dup output fd";
    return false;
  }

  base::File outfile(dup_fd);
  if (!base::CopyFileContents(tarfile, outfile)) {
    PLOG(ERROR) << "Failed to send binary logs";
    return false;
  }

  return true;
}

std::optional<base::FilePath> GetDaemonStorePath(
    org::chromium::CryptohomeMiscInterfaceProxyInterface* proxy,
    base::FilePath& daemon_store_base_dir,
    const std::string& username) {
  user_data_auth::GetSanitizedUsernameRequest request;
  user_data_auth::GetSanitizedUsernameReply reply;
  request.set_username(username);
  brillo::ErrorPtr error;
  if (!proxy->GetSanitizedUsername(request, &reply, &error) || error.get()) {
    LOG(ERROR) << "Failed to retrieve sanitized username: "
               << error->GetMessage();
    return std::nullopt;
  }
  if (reply.sanitized_username().empty()) {
    LOG(ERROR) << "Retrieved emtpy sanitized username.";
    return std::nullopt;
  }
  return daemon_store_base_dir.Append(reply.sanitized_username());
}

}  // namespace

namespace debugd {

BinaryLogTool::BinaryLogTool(scoped_refptr<dbus::Bus> bus)
    : fbpreprocessor_proxy_(
          std::make_unique<org::chromium::FbPreprocessorProxy>(bus)),
      cryptohome_proxy_(
          std::make_unique<org::chromium::CryptohomeMiscInterfaceProxy>(bus)),
      daemon_store_base_dir_(
          base::FilePath(fbpreprocessor::kDaemonStorageRoot)) {}

void BinaryLogTool::DisableMinijailForTesting() {
  use_minijail_ = false;
}

void BinaryLogTool::SetFbPreprocessorProxyForTesting(
    std::unique_ptr<org::chromium::FbPreprocessorProxyInterface> proxy) {
  fbpreprocessor_proxy_ = std::move(proxy);
}

void BinaryLogTool::SetCryptohomeProxyForTesting(
    std::unique_ptr<org::chromium::CryptohomeMiscInterfaceProxyInterface>
        proxy) {
  cryptohome_proxy_ = std::move(proxy);
}

void BinaryLogTool::GetBinaryLogs(
    const std::string& username,
    const std::map<FeedbackBinaryLogType, base::ScopedFD>& outfds) {
  if (!outfds.contains(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP) &&
      !outfds.contains(FeedbackBinaryLogType::BLUETOOTH_FIRMWARE_DUMP)) {
    LOG(ERROR) << "Unsupported binary log type";
    return;
  }

  fbpreprocessor::DebugDumps dumps;
  brillo::ErrorPtr error;
  if (!fbpreprocessor_proxy_->GetDebugDumps(&dumps, &error) || error.get()) {
    LOG(ERROR) << "Failed to retrieve debug dumps: " << error->GetMessage();
    return;
  }

  // GetDaemonStorePath() returns "/run/daemon-store/<daemon_name>/<user_hash>"
  // path, which is the preferred place to store per-user data.
  std::optional<base::FilePath> daemon_store_path = GetDaemonStorePath(
      cryptohome_proxy_.get(), daemon_store_base_dir_, username);
  if (!daemon_store_path) {
    LOG(ERROR) << "Failed to get the daemon store path";
    return;
  }

  for (auto const& [type, outfd] : outfds) {
    std::set<base::FilePath> files;

    for (auto dump : dumps.dump()) {
      base::FilePath file;

      if (dump.has_wifi_dump() &&
          type == FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP) {
        file = base::FilePath(dump.wifi_dump().dmpfile());
      }
      if (dump.has_bluetooth_dump() &&
          type == FeedbackBinaryLogType::BLUETOOTH_FIRMWARE_DUMP) {
        file = base::FilePath(dump.bluetooth_dump().dmpfile());
      }

      if (base::PathExists(file)) {
        files.insert(file);
      }
    }

    if (files.empty()) {
      continue;
    }

    if (!ValidateDirectoryNames(files, daemon_store_path.value())) {
      LOG(ERROR) << "Failed to validate binary log files";
      continue;
    }

    std::optional<base::FilePath> tarball_name = GetTarballName(type);
    if (!tarball_name) {
      LOG(ERROR) << "Failed to get valid compressed file name for type "
                 << type;
      continue;
    }

    if (!CompressAndSendFilesToFD(tarball_name.value(), files,
                                  daemon_store_path.value(), use_minijail_,
                                  outfd.get())) {
      LOG(ERROR) << "Failed to send binary logs " << tarball_name.value();
      continue;
    }
  }
}

}  // namespace debugd
