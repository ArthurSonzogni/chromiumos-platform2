// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file gets compiled into the 'cryptohome-namespace-mounter' executable.
// This executable performs an ephemeral mount (for Guest sessions) on behalf of
// cryptohome.
// Eventually, this executable will perform all cryptohome mounts.
// The lifetime of this executable's process matches the lifetime of the mount:
// it's launched by cryptohome when a session is started, and it's
// killed by cryptohome when the session exits.

#include <sysexits.h>

#include <map>
#include <memory>
#include <vector>

#include <base/at_exit.h>
#include <base/callback.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <brillo/asynchronous_signal_handler.h>
#include <brillo/cryptohome.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/scoped_mount_namespace.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <dbus/cryptohome/dbus-constants.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/storage/mount_constants.h"
#include "cryptohome/storage/mount_helper.h"
#include "cryptohome/storage/mount_utils.h"

#include "cryptohome/namespace_mounter_ipc.pb.h"

using base::FilePath;

namespace {

std::map<cryptohome::MountType, cryptohome::OutOfProcessMountRequest_MountType>
    kProtobufMountType = {
        // Not mounted.
        {cryptohome::MountType::NONE,
         cryptohome::OutOfProcessMountRequest_MountType_NONE},
        // Encrypted with ecryptfs.
        {cryptohome::MountType::ECRYPTFS,
         cryptohome::OutOfProcessMountRequest_MountType_ECRYPTFS},
        // Encrypted with dircrypto.
        {cryptohome::MountType::DIR_CRYPTO,
         cryptohome::OutOfProcessMountRequest_MountType_DIR_CRYPTO},
        // Ephemeral mount.
        {cryptohome::MountType::EPHEMERAL,
         cryptohome::OutOfProcessMountRequest_MountType_EPHEMERAL},
        // Encrypted with dm-crypt.
        {cryptohome::MountType::DMCRYPT,
         cryptohome::OutOfProcessMountRequest_MountType_DMCRYPT}};

const std::vector<FilePath> kDaemonDirPaths = {
    FilePath("session_manager"), FilePath("shill"), FilePath("shill_logs")};

void CleanUpGuestDaemonDirectories(cryptohome::Platform* platform) {
  FilePath root_home_dir = brillo::cryptohome::home::GetRootPath(
      brillo::cryptohome::home::kGuestUserName);
  if (!platform->DirectoryExists(root_home_dir)) {
    // No previous Guest sessions have been started, do nothing.
    return;
  }

  for (const FilePath& daemon_path : kDaemonDirPaths) {
    FilePath to_delete = root_home_dir.Append(daemon_path);
    if (platform->DirectoryExists(to_delete)) {
      LOG(INFO) << "Attempting to delete " << to_delete.value();
      if (!platform->DeletePathRecursively(to_delete)) {
        LOG(WARNING) << "Failed to delete " << to_delete.value();
      }
    }
  }
}

bool HandleSignal(base::RepeatingClosure quit_closure,
                  const struct signalfd_siginfo&) {
  VLOG(1) << "Got signal";
  std::move(quit_closure).Run();
  return true;  // unregister the handler
}

void TearDownEphemeralAndReportError(
    cryptohome::MountHelperInterface* mounter) {
  if (!mounter->TearDownEphemeralMount()) {
    ReportCryptohomeError(cryptohome::kEphemeralCleanUpFailed);
  }
}

}  // namespace

int main(int argc, char** argv) {
  brillo::BaseMessageLoop message_loop;
  message_loop.SetAsCurrent();

  brillo::AsynchronousSignalHandler sig_handler;
  sig_handler.Init();

  brillo::InitLog(brillo::kLogToSyslog);

  cryptohome::ScopedMetricsInitializer metrics;

  constexpr uid_t uid = 1000;         // UID for 'chronos'.
  constexpr gid_t gid = 1000;         // GID for 'chronos'.
  constexpr gid_t access_gid = 1001;  // GID for 'chronos-access'.

  cryptohome::OutOfProcessMountRequest request;
  if (!cryptohome::ReadProtobuf(STDIN_FILENO, &request)) {
    LOG(ERROR) << "Failed to read request protobuf";
    return EX_NOINPUT;
  }

  brillo::SecureBlob system_salt;
  brillo::SecureBlob::HexStringToSecureBlob(request.system_salt(),
                                            &system_salt);

  cryptohome::Platform platform;

  // Before performing any mounts, check whether there are any leftover
  // Guest session daemon directories in /home/root/<hashed username>/.
  // See crbug.com/1069501 for details.
  if (request.username() == brillo::cryptohome::home::kGuestUserName) {
    CleanUpGuestDaemonDirectories(&platform);
  }

  std::unique_ptr<brillo::ScopedMountNamespace> ns_mnt;
  if (!request.mount_namespace_path().empty()) {
    // Enter the required mount namespace.
    ns_mnt = brillo::ScopedMountNamespace::CreateFromPath(
        base::FilePath(request.mount_namespace_path()));
    // cryptohome_namespace_mounter will only fail if it cannot enter the
    // existing user session mount namespace. If the namespace doesn't exist
    // cryptohome_namespace_mounter will do the mounts in the root mount
    // namespace. The design here is consistent with the session_manager
    // behavior which will continue in the root mount namespace if the namespace
    // creation is attempted but failed. The failure in the namespace creation
    // is a very rare corner case and the user session will continue in the
    // root mount namespace if that happens.
    if (ns_mnt == nullptr && cryptohome::UserSessionMountNamespaceExists()) {
      cryptohome::ForkAndCrash(
          "cryptohome failed to enter the existing user session mount "
          "namespace");
      return EX_OSERR;
    }
  }

  cryptohome::MountHelper mounter(uid, gid, access_gid, system_salt,
                                  request.legacy_home(),
                                  request.bind_mount_downloads(), &platform);

  cryptohome::MountError error = cryptohome::MOUNT_ERROR_NONE;
  // Link the user keyring into session keyring to allow request_key() search
  // for ecryptfs mounts.
  if (!platform.SetupProcessKeyring()) {
    LOG(ERROR) << "Failed to set up a process keyring.";
    error = cryptohome::MOUNT_ERROR_SETUP_PROCESS_KEYRING_FAILED;
    return EX_OSERR;
  }

  // A failure in PerformMount/PerformEphemeralMount might still require
  // clean-up so set up the clean-up routine before
  // PerformMount/PerformEphemeralMount is started.
  base::ScopedClosureRunner tear_down_runner;
  cryptohome::OutOfProcessMountResponse response;
  bool is_ephemeral =
      request.type() == kProtobufMountType[cryptohome::MountType::EPHEMERAL];
  if (is_ephemeral) {
    tear_down_runner = base::ScopedClosureRunner(
        base::BindOnce(&TearDownEphemeralAndReportError, &mounter));

    cryptohome::ReportTimerStart(cryptohome::kPerformEphemeralMountTimer);
    if (!mounter.PerformEphemeralMount(request.username())) {
      cryptohome::ForkAndCrash("PerformEphemeralMount failed");
      return EX_SOFTWARE;
    }

    cryptohome::ReportTimerStop(cryptohome::kPerformEphemeralMountTimer);
    VLOG(1) << "PerformEphemeralMount succeeded";
  } else {
    tear_down_runner = base::ScopedClosureRunner(
        base::BindOnce(&cryptohome::MountHelper::TearDownNonEphemeralMount,
                       base::Unretained(&mounter)));

    cryptohome::MountHelperInterface::Options mount_options;
    mount_options.type = static_cast<cryptohome::MountType>(request.type());
    mount_options.to_migrate_from_ecryptfs = request.to_migrate_from_ecryptfs();

    cryptohome::ReportTimerStart(cryptohome::kPerformMountTimer);
    if (!mounter.PerformMount(mount_options, request.username(),
                              request.fek_signature(), request.fnek_signature(),
                              request.is_pristine(), &error)) {
      cryptohome::ForkAndCrash("PerformMount failed");
      return EX_SOFTWARE;
    }

    cryptohome::ReportTimerStop(cryptohome::kPerformMountTimer);
    response.set_mount_error(static_cast<uint32_t>(error));
    VLOG(1) << "PerformMount succeeded";
  }

  for (const auto& path : mounter.MountedPaths()) {
    response.add_paths(path.value());
  }

  if (!cryptohome::WriteProtobuf(STDOUT_FILENO, response)) {
    cryptohome::ForkAndCrash("Failed to write response protobuf");
    return EX_OSERR;
  }
  VLOG(1) << "Sent protobuf";

  base::RunLoop run_loop;

  // |STDIN_FILENO| is the read end of a pipe whose write end is a file
  // descriptor in 'cryptohomed'. |WatchReadable()| will execute the callback
  // when |STDIN_FILENO| can be read without blocking, or when there is a pipe
  // error. The code does not need to read any more input from 'cryptohomed' at
  // this point so the only expected event on the pipe is the write end of the
  // pipe being closed because of a 'cryptohomed' crash.
  // The resulting behavior is that the code will quit the run loop, clean up
  // the mount, and exit if 'cryptohomed' crashes.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher =
      base::FileDescriptorWatcher::WatchReadable(STDIN_FILENO,
                                                 run_loop.QuitClosure());

  // Quit the run loop when signalled.
  sig_handler.RegisterHandler(
      SIGTERM, base::BindRepeating(&HandleSignal, run_loop.QuitClosure()));

  run_loop.Run();

  // |tear_down_runner| will clean up the mount now.
  return EX_OK;
}
