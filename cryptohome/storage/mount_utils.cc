// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains assorted functions used in mount-related classed.

#include "cryptohome/storage/mount_utils.h"

#include <linux/magic.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <unordered_map>
#include <vector>

#include <base/files/file_util.h>
#include <chromeos/constants/cryptohome.h>

namespace {
// Size of span when writing protobuf message size to file.
constexpr int kSpanSize = 1;

}  // namespace

namespace cryptohome {

bool UserSessionMountNamespaceExists() {
  struct statfs buff;
  if (statfs(kUserSessionMountNamespacePath, &buff) == 0) {
    if (static_cast<uint64_t>(buff.f_type) != NSFS_MAGIC) {
      LOG(ERROR) << kUserSessionMountNamespacePath
                 << " is not a namespace file, has the user session namespace "
                    "been created?";
      return false;
    }
  } else {
    PLOG(ERROR) << "statfs(" << kUserSessionMountNamespacePath << ") failed";
    return false;
  }
  return true;
}

bool ReadProtobuf(int in_fd, google::protobuf::MessageLite* message) {
  size_t proto_size = 0;
  if (!base::ReadFromFD(in_fd, reinterpret_cast<char*>(&proto_size),
                        sizeof(proto_size))) {
    PLOG(ERROR) << "Failed to read protobuf size";
    return false;
  }

  std::vector<char> buf(proto_size);
  if (!base::ReadFromFD(in_fd, buf.data(), buf.size())) {
    PLOG(ERROR) << "Failed to read protobuf";
    return false;
  }

  if (!message->ParseFromArray(buf.data(), buf.size())) {
    LOG(ERROR) << "Failed to parse protobuf";
    return false;
  }

  return true;
}

bool WriteProtobuf(int out_fd, const google::protobuf::MessageLite& message) {
  size_t size = message.ByteSizeLong();
  if (!base::WriteFileDescriptor(
          out_fd, base::as_bytes(base::make_span(&size, kSpanSize)))) {
    PLOG(ERROR) << "Failed to write protobuf size";
    return false;
  }

  if (!message.SerializeToFileDescriptor(out_fd)) {
    LOG(ERROR) << "Failed to serialize and write protobuf";
    return false;
  }

  return true;
}

void ForkAndCrash(const std::string& message) {
  pid_t child_pid = fork();

  if (child_pid < 0) {
    PLOG(ERROR) << "fork() failed";
  } else if (child_pid == 0) {
    // Child process: crash with |message|.
    LOG(FATAL) << message;
  } else {
    // |child_pid| > 0
    // Parent process: reap the child process in a best-effort way and return
    // normally.
    waitpid(child_pid, nullptr, 0);
  }
}

user_data_auth::CryptohomeErrorCode MountErrorToCryptohomeError(
    const MountError code) {
  static const std::unordered_map<MountError,
                                  user_data_auth::CryptohomeErrorCode>
      error_code_lut = {
          {MOUNT_ERROR_NONE, user_data_auth::CRYPTOHOME_ERROR_NOT_SET},
          {MOUNT_ERROR_FATAL, user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL},
          {MOUNT_ERROR_KEY_FAILURE,
           user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED},
          {MOUNT_ERROR_MOUNT_POINT_BUSY,
           user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY},
          {MOUNT_ERROR_TPM_COMM_ERROR,
           user_data_auth::CRYPTOHOME_ERROR_TPM_COMM_ERROR},
          {MOUNT_ERROR_UNPRIVILEGED_KEY,
           user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED},
          {MOUNT_ERROR_TPM_DEFEND_LOCK,
           user_data_auth::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK},
          {MOUNT_ERROR_TPM_UPDATE_REQUIRED,
           user_data_auth::CRYPTOHOME_ERROR_TPM_UPDATE_REQUIRED},
          {MOUNT_ERROR_USER_DOES_NOT_EXIST,
           user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND},
          {MOUNT_ERROR_TPM_NEEDS_REBOOT,
           user_data_auth::CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT},
          {MOUNT_ERROR_OLD_ENCRYPTION,
           user_data_auth::CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION},
          {MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE,
           user_data_auth::
               CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE},
          {MOUNT_ERROR_RECREATED, user_data_auth::CRYPTOHOME_ERROR_NOT_SET},
          {MOUNT_ERROR_VAULT_UNRECOVERABLE,
           user_data_auth::CRYPTOHOME_ERROR_VAULT_UNRECOVERABLE}};

  if (error_code_lut.count(code) != 0) {
    return error_code_lut.at(code);
  }

  return user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL;
}

}  // namespace cryptohome
