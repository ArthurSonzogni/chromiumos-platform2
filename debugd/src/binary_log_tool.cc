// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/binary_log_tool.h"

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/containers/span.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/memory/scoped_refptr.h>
#include <base/logging.h>
#include <chromeos/dbus/fbpreprocessor/dbus-constants.h>
#include <brillo/cryptohome.h>
#include <brillo/errors/error.h>
#include <dbus/debugd/dbus-constants.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <fbpreprocessor-client/fbpreprocessor/dbus-proxies.h>

namespace {

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

}  // namespace

namespace debugd {

BinaryLogTool::BinaryLogTool(scoped_refptr<dbus::Bus> bus)
    : fbpreprocessor_proxy_(
          std::make_unique<org::chromium::FbPreprocessorProxy>(bus)) {}

void BinaryLogTool::GetBinaryLogs(
    const std::string& username,
    const std::map<FeedbackBinaryLogType, base::ScopedFD>& outfds) {
  if (!outfds.contains(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP)) {
    LOG(ERROR) << "Unsupported binary log type";
    return;
  }

  fbpreprocessor::DebugDumps dumps;
  brillo::ErrorPtr error;
  if (!fbpreprocessor_proxy_->GetDebugDumps(&dumps, &error) || error.get()) {
    LOG(ERROR) << "Failed to retrieve debug dumps: " << error->GetMessage();
    return;
  }

  std::set<base::FilePath> files;

  for (auto dump : dumps.dump()) {
    if (dump.has_wifi_dump()) {
      base::FilePath file(dump.wifi_dump().dmpfile());
      if (base::PathExists(file)) {
        files.insert(file);
      }
    }
  }

  if (files.empty()) {
    return;
  }

  // GetDaemonStorePath() returns "/run/daemon-store/<daemon_name>/<user_hash>"
  // path, which is the preferred place to store per-user data.
  base::FilePath daemon_store_path =
      brillo::cryptohome::home::GetDaemonStorePath(
          brillo::cryptohome::home::Username(username),
          fbpreprocessor::kDaemonName);

  if (daemon_store_path.empty()) {
    LOG(ERROR) << "Failed to get the daemon store path";
    return;
  }

  if (!ValidateDirectoryNames(files, daemon_store_path)) {
    LOG(ERROR) << "Failed to validate binary log files";
    return;
  }

  int out_fd = outfds.at(FeedbackBinaryLogType::WIFI_FIRMWARE_DUMP).get();

  // TODO(b/291347317): Placeholder code. Send sample data for testing.
  // Implement binary log collection.
  constexpr std::string_view sample_data = "test data";
  if (!base::WriteFileDescriptor(out_fd, sample_data)) {
    PLOG(ERROR) << "Failed to send binary log";
    return;
  }
}

}  // namespace debugd
