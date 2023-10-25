// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_PLATFORM_H_
#define DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_PLATFORM_H_

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/functional/callback_forward.h>
#include <base/unguessable_token.h>
#include <brillo/blkdev_utils/loop_device.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/brillo_export.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest_prod.h>

extern "C" {
#include <linux/fs.h>
}

namespace device_management {

// Platform specific routines abstraction layer.
// Also helps us to be able to mock them in tests.
class BRILLO_EXPORT Platform {
 public:
  Platform();
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  virtual ~Platform();

  // Reads a file completely into a blob/string.
  //
  // Parameters
  //  path              - Path of the file to read
  //  blob/string (OUT) - blob/string to populate
  virtual bool ReadFile(const base::FilePath& path, brillo::Blob* blob);

  // Atomically and durably writes the entirety of the given data to |path| with
  // |mode| permissions (modulo umask).  If missing, parent (and parent of
  // parent etc.)  directories are created with 0700 permissions (modulo umask).
  // Returns true if the file has been written successfully and it has
  // physically hit the disk.  Returns false if either writing the file has
  // failed or if it cannot be guaranteed that it has hit the disk.
  //
  // Parameters
  //  path      - Path of the file to write
  //  blob/data - blob/string to populate from
  //  mode      - File permission bit-pattern, eg. 0644 for rw-r--r--
  virtual bool WriteFileAtomicDurable(const base::FilePath& path,
                                      const brillo::Blob& blob,
                                      mode_t mode);
  // Atomically writes the entirety of the given data to |path| with |mode|
  // permissions (modulo umask).  If missing, parent (and parent of parent etc.)
  // directories are created with 0700 permissions (modulo umask).  Returns true
  // if the file has been written successfully and it has physically hit the
  // disk.  Returns false if either writing the file has failed or if it cannot
  // be guaranteed that it has hit the disk.
  //
  // Parameters
  //   path - Path of the file to write
  //   data - Blob to populate from
  //   mode - File permission bit-pattern, eg. 0644 for rw-r--r--
  virtual bool WriteFileAtomic(const base::FilePath& path,
                               const brillo::Blob& blob,
                               mode_t mode);
  // Returns true if the specified file exists.
  //
  // Parameters
  //  path - Path of the file to check
  virtual bool FileExists(const base::FilePath& path) const;

  // Delete the given path.
  //
  // Parameters
  //  path - string containing file path to delete
  virtual bool DeleteFile(const base::FilePath& path);

  // Calls fsync() on directory.  Returns true on success.
  //
  // Parameters
  //   path - Directory to be sync'ed
  virtual bool SyncDirectory(const base::FilePath& path);

 private:
  // Calls fdatasync() on file if data_sync is true or fsync() on directory or
  // file when data_sync is false.  Returns true on success.
  //
  // Parameters
  //   path - File/directory to be sync'ed
  //   is_directory - True if |path| is a directory
  //   data_sync - True if |path| does not need metadata to be synced
  bool SyncFileOrDirectory(const base::FilePath& path,
                           bool is_directory,
                           bool data_sync);
};
}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_PLATFORM_H_
