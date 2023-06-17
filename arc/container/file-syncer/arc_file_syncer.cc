// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <selinux/selinux.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#include <memory>
#include <vector>

#include <base/at_exit.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <base/files/file_path_watcher.h>
#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/file_utils.h>
#include <brillo/files/file_util.h>
#include <brillo/files/safe_fd.h>

/**
 * This utility performs bi-directional file synchronization for the set of
 * predefined control files. For each file it creates copy of it and activates
 * watching for file change. Each change in the source file is propagated to
 * the copy file. And change in the copy file is propagated to the source file.
 * This allows creating bind mapping of the copy file in container namespace.
 * As a result the content of source file in host namespace and container
 * namespace is synchronized.
 */

namespace {

// Root directory to keep copied files.
constexpr char kSyncerDir[] = "/var/run/arc/file-syncer";
// Configuration file for container that contains mounts.
constexpr char kContainerConfig[] =
    "/opt/google/containers/android/config.json";

constexpr uid_t kAndroidShiftUid = 655360;
constexpr gid_t kAndroidShiftGid = 655360;

// This copies |host_file| to |guest_file| with attributes, such as mode, owner
// and SELinux context. Note, that owner is shifted to match owner in Android
// namespace.
bool CopyFileWithAttributes(const base::FilePath& host_file,
                            const base::FilePath& guest_file) {
  if (!base::CopyFile(host_file, guest_file)) {
    PLOG(ERROR) << "Failed to copy " << host_file.value() << " -> "
                << guest_file.value();
    return false;
  }

  struct stat info;
  if (stat(host_file.value().c_str(), &info) < 0) {
    PLOG(ERROR) << "Failed to get host file info " << host_file.value();
    return false;
  }

  char* con;
  if (lgetfilecon(host_file.value().c_str(), &con) < 0) {
    PLOG(ERROR) << "Failed to get host file SELinux context "
                << host_file.value();
    return false;
  }
  std::string se_context = con;
  freecon(con);

  base::ScopedFD fd(brillo::OpenSafely(guest_file, O_RDONLY, 0));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open guest file " << guest_file.value();
    return false;
  }
  if (fchmod(fd.get(), info.st_mode) < 0) {
    PLOG(ERROR) << "Failed to set guest file mode " << guest_file.value();
    return false;
  }
  if (fchown(fd.get(), info.st_uid + kAndroidShiftUid,
             info.st_gid + kAndroidShiftGid) < 0) {
    PLOG(ERROR) << "Failed to set guest file owner " << guest_file.value();
    return false;
  }
  if (fsetfilecon(fd.get(), se_context.c_str()) < 0) {
    PLOG(ERROR) << "Failed to set guest file SELinux context "
                << guest_file.value() << " " << se_context;
    return false;
  }

  return true;
}

// This creates vector of pair of files for synchronization. In order to avoid
// file list declaration duplicates it uses
// /opt/google/containers/android/config.json as source of truth. This
// configuration contains mount points into container namespace. It looks
// for sources that has parent directory |file_syncer_dir| and extracts
// corresponding destination.
std::vector<std::pair<base::FilePath, base::FilePath>> GetFilesForSync(
    const base::FilePath& file_syncer_dir) {
  const base::FilePath config_json(kContainerConfig);
  std::string json_str;
  if (!base::ReadFileToString(config_json, &json_str)) {
    PLOG(FATAL) << "Failed to read json string from " << config_json.value();
  }

  auto result_json = base::JSONReader::ReadAndReturnValueWithError(
      json_str, base::JSON_PARSE_RFC);
  if (!result_json.has_value() || !result_json->is_dict()) {
    LOG(FATAL) << "Failed to parse json: " << result_json.error().message;
  }

  const auto* mounts = result_json->GetDict().Find("mounts");
  if (!mounts || !mounts->is_list()) {
    LOG(FATAL) << "Failed to find mounts entry";
  }

  std::vector<std::pair<base::FilePath, base::FilePath>> result;
  for (const auto& mount : mounts->GetList()) {
    if (!mount.is_dict()) {
      continue;
    }
    const auto* source = mount.GetDict().Find("source");
    if (!source || !source->is_string()) {
      continue;
    }
    const base::FilePath source_path(source->GetString());
    if (source_path.DirName() != file_syncer_dir) {
      continue;
    }
    const auto* destination = mount.GetDict().Find("destination");
    if (!destination || !destination->is_string()) {
      LOG(FATAL) << "No destination for " << source_path.value();
    }
    result.push_back({base::FilePath(destination->GetString()), source_path});
  }

  return result;
}

// FileSyncerEntry represents one synchronization pair.
class FileSyncerEntry {
 public:
  FileSyncerEntry(const base::FilePath& host_file,
                  const base::FilePath& guest_file)
      : host_file_(host_file), guest_file_(guest_file) {
    if (!CopyFileWithAttributes(host_file_, guest_file_)) {
      LOG(FATAL) << "Failed to initialize sync " << host_file_.value() << " -> "
                 << guest_file_.value();
    }

    if (!host_file_watcher_.Watch(
            host_file_, base::FilePathWatcher::Type::kNonRecursive,
            base::BindRepeating(&FileSyncerEntry::OnHostFileChanged,
                                base::Unretained(this)))) {
      LOG(FATAL) << "Failed to start host watcher " << host_file_.value();
    }

    if (!guest_file_watcher_.Watch(
            guest_file_, base::FilePathWatcher::Type::kNonRecursive,
            base::BindRepeating(&FileSyncerEntry::OnGuestFileChanged,
                                base::Unretained(this)))) {
      LOG(FATAL) << "Failed to start guest watcher " << guest_file_.value();
    }

    LOG(INFO) << "Syncing " << host_file_.value() << " <-> "
              << guest_file_.value();
  }

  FileSyncerEntry(const FileSyncerEntry&) = delete;
  FileSyncerEntry& operator=(const FileSyncerEntry&) = delete;

  ~FileSyncerEntry() = default;

 private:
  void OnHostFileChanged(const base::FilePath& path, bool error) {
    if (error) {
      return;
    }
    if (!ShouldSync()) {
      return;
    }

    if (!base::CopyFile(host_file_, guest_file_)) {
      PLOG(ERROR) << "Failed to sync " << host_file_.value() << " -> "
                  << guest_file_.value();
      return;
    }

    LOG(INFO) << "Guest file " << guest_file_ << " synced from host";
  }

  void OnGuestFileChanged(const base::FilePath& path, bool error) {
    if (error) {
      return;
    }
    if (!ShouldSync()) {
      return;
    }

    if (!base::CopyFile(guest_file_, host_file_)) {
      PLOG(ERROR) << "Failed to sync " << guest_file_.value() << " -> "
                  << host_file_.value();
      return;
    }

    LOG(INFO) << "Host file " << host_file_ << " synced from guest";
  }

  // It returns true in case content of source and guest files are different
  // and synchronization is required.
  bool ShouldSync() {
    std::string host_content;
    if (!base::ReadFileToString(host_file_, &host_content)) {
      PLOG(ERROR) << "Failed to read " << host_file_;
      return false;
    }
    std::string guest_content;
    if (!base::ReadFileToString(guest_file_, &guest_content)) {
      PLOG(ERROR) << "Failed to read " << guest_file_;
      return false;
    }
    return host_content != guest_content;
  }

  const base::FilePath host_file_;
  const base::FilePath guest_file_;

  base::FilePathWatcher host_file_watcher_;
  base::FilePathWatcher guest_file_watcher_;
};

// Root class which is used as a holder for synchronization pairs.
class FileSyncer {
 public:
  FileSyncer() : file_syncer_dir_(kSyncerDir) {
    if (!brillo::MkdirRecursively(file_syncer_dir_, 0755).is_valid()) {
      PLOG(FATAL) << "Failed to create " << file_syncer_dir_.value();
    }

    for (const auto& pair : GetFilesForSync(file_syncer_dir_)) {
      entries_.emplace_back(
          std::make_unique<FileSyncerEntry>(pair.first, pair.second));
    }

    LOG(INFO) << "Start monitoring";
  }

  FileSyncer(const FileSyncer&) = delete;
  FileSyncer& operator=(const FileSyncer&) = delete;

  ~FileSyncer() {
    brillo::DeletePathRecursively(file_syncer_dir_);

    LOG(INFO) << "Stopped monitoring";
  }

 private:
  const base::FilePath file_syncer_dir_;
  std::vector<std::unique_ptr<FileSyncerEntry>> entries_;
};

}  // namespace

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(task_executor.task_runner());

  base::RunLoop run_loop;

  FileSyncer syncer;

  run_loop.Run();

  return 0;
}
