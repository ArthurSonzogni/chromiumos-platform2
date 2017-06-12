// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CRYPTOHOME_DIRCRYPTO_DATA_MIGRATOR_MIGRATION_HELPER_H_
#define CRYPTOHOME_DIRCRYPTO_DATA_MIGRATOR_MIGRATION_HELPER_H_

#include <map>
#include <string>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>
#include <chromeos/dbus/service_constants.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/platform.h"

namespace base {
class Thread;
}

namespace cryptohome {
namespace dircrypto_data_migrator {

extern const base::FilePath::CharType kMigrationStartedFileName[];
extern const base::FilePath::CharType kSkippedFileListFileName[];

// A helper class for migrating files to new file system with small overhead of
// diskspace.
// This class is only designed to migrate data from ecryptfs to ext4 encryption,
// and therefore makes some assumptions about the underlying file systems.  In
// particular:
//   Sparse files in the source tree are not supported.  They will be treated as
//   normal files, and therefore cause disk usage to increase after the
//   migration.
//   Support for sparse files in the destination tree are required.  If they are
//   not supported a minimum free space equal to the largest single file on disk
//   will be required for the migration.
//   The destination filesystem needs to support flushing hardware buffers on
//   fsync.  In the case of Ext4, this means not disabling the barrier mount
//   option.
class MigrationHelper {
 public:
  // Callback for monitoring migration progress.  The first parameter is the
  // number of bytes migrated so far, and the second parameter is the total
  // number of bytes that need to be migrated, including what has already been
  // migrated.  If status is not DIRCRYPTO_MIGRATION_IN_PROGRESS the values in
  // migrated should be ignored as they are undefined.
  using ProgressCallback = base::Callback<void(
      DircryptoMigrationStatus status, uint64_t migrated, uint64_t total)>;

  // Creates a new MigrationHelper for migrating from |from| to |to|.
  // Status files will be stored in |status_files_dir|, which should not be in
  // the directory tree to be migrated.  |max_chunk_size| is treated as a hint
  // for the desired size of data to transfer at once, but may be reduced if
  // there is not enough free space on disk or the provided max_chunk_size is
  // inefficient.
  MigrationHelper(Platform* platform,
                  const base::FilePath& from,
                  const base::FilePath& to,
                  const base::FilePath& status_files_dir,
                  uint64_t max_chunk_size);
  virtual ~MigrationHelper();

  void set_namespaced_mtime_xattr_name_for_testing(const std::string& name) {
    namespaced_mtime_xattr_name_ = name;
  }
  void set_namespaced_atime_xattr_name_for_testing(const std::string& name) {
    namespaced_atime_xattr_name_ = name;
  }
  void set_num_job_threads_for_testing(size_t num_job_threads) {
    num_job_threads_ = num_job_threads;
  }
  void set_max_job_list_size_for_testing(size_t max_job_list_size) {
    max_job_list_size_ = max_job_list_size;
  }

  // Moves all files under |from| into |to| specified in the constructor.
  //
  // This function copies chunks of a file at a time, requiring minimal free
  // space overhead.  This method should only ever be called once in the
  // lifetime of the object.
  //
  // Parameters
  //   progress_callback - function that will be called regularly to update on
  //   the progress of the migration.  Callback may be executed from one of the
  //   job processing threads or the caller thread, so long-running callbacks
  //   may block the migration.  May not be null.
  bool Migrate(const ProgressCallback& progress_callback);

  // Returns true if the migration has been started, but not finished.
  bool IsMigrationStarted() const;

 private:
  FRIEND_TEST(MigrationHelperTest, CopyOwnership);

  struct Job;
  class WorkerPool;

  // Calculate the total number of bytes to be migrated, populating
  // |total_byte_count_| with the result.
  void CalculateDataToMigrate(const base::FilePath& from);
  // Increment the number of bytes migrated, potentially reporting the status if
  // its time for a new report.
  void IncrementMigratedBytes(uint64_t bytes);
  // Call |progress_callback_| with the number of bytes already migrated, the
  // total number of bytes to be migrated, and the migration status.
  void ReportStatus(DircryptoMigrationStatus status);
  // Creates a new directory that is the result of appending |child| to |to|,
  // migrating recursively all contents of the source directory.
  //
  // Parameters
  //   child - relative path under the base path to migrate.
  //   worker_pool - Worker pool to dispatch jobs to job threads.
  bool MigrateDir(const base::FilePath& child,
                  const FileEnumerator::FileInfo& info,
                  WorkerPool* worker_pool);
  // Creates a new link |to_base_path_|/|child| which has the same attributes
  // and target as |from_base_path_|/|child|.  If the target points to an
  // absolute path under |from_base_path_|, it is rewritten to point to the
  // same relative path under |to_base_path_|.
  bool MigrateLink(const base::FilePath& child,
                   const FileEnumerator::FileInfo& info);
  // Copies data from |from_base_path_|/|child| to |to_base_path_|/|child|.
  bool MigrateFile(const base::FilePath& child,
                   const FileEnumerator::FileInfo& info);
  bool CopyAttributes(const base::FilePath& child,
                      const FileEnumerator::FileInfo& info);
  bool FixTimes(const base::FilePath& child);
  // Remove the temporary xattrs used to store atime and mtime.
  bool RemoveTimeXattrs(const base::FilePath& child);
  bool CopyExtendedAttributes(const base::FilePath& child);
  bool SetExtendedAttributeIfNotPresent(const base::FilePath& file,
                                        const std::string& xattr,
                                        const char* value,
                                        ssize_t size);
  // Record the latest file error happened during the migration.
  // |operation| is the type of the operation cause the |error| and
  // |child| is the path of migrated file from the root of migration.
  //
  // We should record the error immediately after the failed low-level
  // file operations (|platform_| methods or base:: functions), not after
  // the batched file operation utility to keep the granularity of the stat
  // and to avoid unintended duplicated logging.
  void RecordFileError(DircryptoMigrationFailedOperationType operation,
                       const base::FilePath& child,
                       base::File::Error error);
  void RecordFileErrorWithCurrentErrno(
      DircryptoMigrationFailedOperationType operation,
      const base::FilePath& child);
  // Records the fact that the file at |rel_path| was skipped during migration.
  void RecordSkippedFile(const base::FilePath& rel_path);

  // Processes the job.
  // Must be called on a job thread.
  bool ProcessJob(const Job& job);

  // Increments the child count of the given directory.
  // Can be called on any thread.
  void IncrementChildCount(const base::FilePath& child);

  // Decrements the child count of the given directory. When the direcotry
  // becomes empty, deletes the directory and recursively cleans up the parent.
  // Can be called on any thread.
  bool DecrementChildCountAndDeleteIfNecessary(const base::FilePath& child);

  Platform* platform_;
  base::FilePath from_base_path_;
  base::FilePath to_base_path_;
  ProgressCallback progress_callback_;
  const base::FilePath status_files_dir_;
  uint64_t max_chunk_size_;
  uint64_t effective_chunk_size_;
  uint64_t total_byte_count_;
  uint64_t total_directory_byte_count_;
  int n_files_;
  int n_dirs_;
  int n_symlinks_;

  uint64_t migrated_byte_count_;
  base::TimeTicks next_report_;
  // Lock for migrated_byte_count_ and next_report_.
  base::Lock migrated_byte_count_lock_;

  std::string namespaced_mtime_xattr_name_;
  std::string namespaced_atime_xattr_name_;
  base::FilePath skipped_file_list_path_;

  DircryptoMigrationFailedOperationType failed_operation_type_;
  DircryptoMigrationFailedPathType failed_path_type_;
  base::File::Error failed_error_type_;
  // Lock for failed_operation_type_, failed_path_type_, and failed_error_type_.
  base::Lock failure_info_lock_;

  size_t num_job_threads_;
  size_t max_job_list_size_;

  std::map<base::FilePath, int> child_counts_;  // Child count for directories.
  base::Lock child_counts_lock_;  // Lock for child_counts_.

  DISALLOW_IMPLICIT_CONSTRUCTORS(MigrationHelper);
};

}  // namespace dircrypto_data_migrator
}  // namespace cryptohome

#endif  // CRYPTOHOME_DIRCRYPTO_DATA_MIGRATOR_MIGRATION_HELPER_H_
