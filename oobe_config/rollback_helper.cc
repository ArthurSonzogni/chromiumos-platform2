// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/rollback_helper.h"

#include <iterator>
#include <set>
#include <vector>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "oobe_config/oobe_config.h"
#include "oobe_config/rollback_constants.h"

namespace oobe_config {

const int kDefaultPwnameLength = 1024;

bool PrepareSave(const base::FilePath& root_path,
                 bool ignore_permissions_for_testing) {
  base::FilePath rollback_data_path = PrefixAbsolutePath(
      root_path, base::FilePath(kUnencryptedStatefulRollbackDataFile));

  if (!ignore_permissions_for_testing) {
    uid_t oobe_config_save_uid;
    gid_t oobe_config_save_gid;
    uid_t root_uid;
    gid_t root_gid;
    gid_t preserve_gid;

    if (!GetUidGid(kOobeConfigSaveUsername, &oobe_config_save_uid,
                   &oobe_config_save_gid)) {
      PLOG(ERROR) << "Couldn't get uid and gid of oobe_config_save.";
      return false;
    }
    if (!GetUidGid(kRootUsername, &root_uid, &root_gid)) {
      PLOG(ERROR) << "Couldn't get uid and gid of root.";
      return false;
    }
    if (!GetGid(kPreserveGroupName, &preserve_gid)) {
      // It's OK for this to fail. This group only exists on TPM2
      // devices.
      LOG(INFO) << "preserve group does not exist on this device";
      preserve_gid = -1;
    } else {
      LOG(INFO) << "preserve group is " << preserve_gid;
    }
    // Preparing rollback_data file.

    // The directory should be root-writeable only on TPM1 devices
    // and root+preserve-writeable on TPM2 devices.
    LOG(INFO) << "Verifying only root and/or preserve can write to stateful";
    std::set<gid_t> allowed_groups = {root_gid};
    if (preserve_gid != -1) {
      allowed_groups.insert(preserve_gid);
    }
    if (!base::VerifyPathControlledByUser(
            PrefixAbsolutePath(root_path,
                               base::FilePath(kStatefulPartitionPath)),
            rollback_data_path.DirName(), root_uid, allowed_groups)) {
      LOG(ERROR) << "VerifyPathControlledByUser failed for "
                 << rollback_data_path.DirName().value();
      return false;
    }

    // Create or wipe the file.
    LOG(INFO) << "Creating an empty owned rollback file and verifying";
    if (base::WriteFile(rollback_data_path, {}, 0) != 0) {
      PLOG(ERROR) << "Couldn't write " << rollback_data_path.value();
      return false;
    }
    // chown oobe_config_save:oobe_config_save
    if (lchown(rollback_data_path.value().c_str(), oobe_config_save_uid,
               oobe_config_save_gid) != 0) {
      PLOG(ERROR) << "Couldn't chown " << rollback_data_path.value();
      return false;
    }
    // chmod 644
    if (chmod(rollback_data_path.value().c_str(), 0644) != 0) {
      PLOG(ERROR) << "Couldn't chmod " << rollback_data_path.value();
      return false;
    }
    // The file should be only writable by kOobeConfigSaveUid.
    if (!base::VerifyPathControlledByUser(
            rollback_data_path, rollback_data_path, oobe_config_save_uid,
            {oobe_config_save_gid})) {
      LOG(ERROR) << "VerifyPathControlledByUser failed for "
                 << rollback_data_path.value();
      return false;
    }
  }

  LOG(INFO) << "Emptying save path";
  base::FilePath save_path =
      PrefixAbsolutePath(root_path, base::FilePath(kSaveTempPath));
  base::FileEnumerator folder_enumerator(
      save_path, false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (auto file = folder_enumerator.Next(); !file.empty();
       file = folder_enumerator.Next()) {
    if (!base::DeletePathRecursively(file)) {
      LOG(ERROR) << "Couldn't delete " << file.value();
    } else {
      LOG(INFO) << "Deleted file: " << file.value();
    }
  }

  LOG(INFO) << "Copying data to save path";
  TryFileCopy(PrefixAbsolutePath(root_path, base::FilePath(kOobeCompletedFile)),
              save_path.Append(kOobeCompletedFileName));
  TryFileCopy(PrefixAbsolutePath(root_path,
                                 base::FilePath(kMetricsReportingEnabledFile)),
              save_path.Append(kMetricsReportingEnabledFileName));

  return true;
}

void CleanupRestoreFiles(const base::FilePath& root_path,
                         const std::set<std::string>& excluded_files) {
  // Delete everything except |excluded_files| in the restore directory.
  base::FilePath restore_path =
      PrefixAbsolutePath(root_path, base::FilePath(kRestoreTempPath));
  base::FileEnumerator folder_enumerator(
      restore_path, false,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
  for (auto file = folder_enumerator.Next(); !file.empty();
       file = folder_enumerator.Next()) {
    if (excluded_files.count(file.value()) == 0) {
      if (!base::DeletePathRecursively(file)) {
        PLOG(ERROR) << "Couldn't delete " << file.value();
      } else {
        LOG(INFO) << "Deleted rollback data file: " << file.value();
      }
    } else {
      LOG(INFO) << "Preserving rollback data file: " << file.value();
    }
  }

  // Delete the original preserved data.
  base::FilePath rollback_data_file = PrefixAbsolutePath(
      root_path, base::FilePath(kUnencryptedStatefulRollbackDataFile));
  if (!base::DeletePathRecursively(rollback_data_file)) {
    PLOG(ERROR) << "Couldn't delete " << rollback_data_file.value();
  } else {
    LOG(INFO) << "Deleted encrypted rollback data.";
  }
}

base::FilePath PrefixAbsolutePath(const base::FilePath& prefix,
                                  const base::FilePath& file_path) {
  if (prefix.empty())
    return file_path;
  DCHECK(!file_path.empty());
  DCHECK_EQ('/', file_path.value()[0]);
  return prefix.Append(file_path.value().substr(1));
}

void TryFileCopy(const base::FilePath& source,
                 const base::FilePath& destination) {
  if (!base::CopyFile(source, destination)) {
    PLOG(WARNING) << "Couldn't copy file " << source.value() << " to "
                  << destination.value();
  } else {
    LOG(INFO) << "Copied " << source.value() << " to " << destination.value();
  }
}

bool GetUidGid(const std::string& user, uid_t* uid, gid_t* gid) {
  // Load the passwd entry.
  int user_name_length = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (user_name_length == -1) {
    user_name_length = kDefaultPwnameLength;
  }
  if (user_name_length < 0) {
    return false;
  }
  passwd user_info{}, *user_infop;
  std::vector<char> user_name_buf(static_cast<size_t>(user_name_length));
  getpwnam_r(user.c_str(), &user_info, user_name_buf.data(),
             static_cast<size_t>(user_name_length), &user_infop);

  // NOTE: the return value can be ambiguous in the case that the user does
  // not exist. See "man getpwnam_r" for details.
  if (user_infop == nullptr) {
    return false;
  }

  *uid = user_info.pw_uid;
  *gid = user_info.pw_gid;
  return true;
}

bool GetGid(const std::string& group, gid_t* gid) {
  int group_name_length = sysconf(_SC_GETGR_R_SIZE_MAX);
  if (group_name_length == -1) {
    group_name_length = kDefaultPwnameLength;
  }
  if (group_name_length < 0) {
    return false;
  }
  struct group group_info {
  }, *group_infop;
  std::vector<char> group_name_buf(static_cast<size_t>(group_name_length));
  getgrnam_r(group.c_str(), &group_info, group_name_buf.data(),
             static_cast<size_t>(group_name_length), &group_infop);

  // NOTE: the return value can be ambiguous in the case that the user does
  // not exist. See "man getgrnam_r" for details.
  if (group_infop == nullptr) {
    return false;
  }

  *gid = group_info.gr_gid;
  return true;
}

}  // namespace oobe_config
