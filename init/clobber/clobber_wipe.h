// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_CLOBBER_CLOBBER_WIPE_H_
#define INIT_CLOBBER_CLOBBER_WIPE_H_

#include <string>

#include <base/files/file_path.h>

#include "init/clobber/clobber_ui.h"

class ClobberWipe {
 public:
  // The index of each partition within the gpt partition table.
  struct PartitionNumbers {
    int stateful = -1;
    int root_a = -1;
    int root_b = -1;
    int kernel_a = -1;
    int kernel_b = -1;
  };

  explicit ClobberWipe(ClobberUi* ui);
  virtual ~ClobberWipe() = default;

  void SetPartitionInfo(const PartitionNumbers& partitions) {
    partitions_ = partitions;
  }

  void SetFastWipe(bool fast_wipe) { fast_wipe_ = fast_wipe; }

  // Wipe `device_path`, showing a progress UI using `ui`.
  //
  // If `fast` is true, wipe `device_path` using a less-thorough but much faster
  // wipe. Not all blocks are guaranteed to be overwritten, so this should be
  // reserved for situations when there is no concern of data leakage.
  // A progress indicator will not be displayed if `fast` mode is enabled.
  virtual bool WipeDevice(const base::FilePath& device_path,
                          bool discard = false);

  // Wrapper around secure_erase_file::SecureErase(const base::FilePath&).
  virtual bool SecureErase(const base::FilePath& path);

  // Wrapper around secure_erase_file::DropCaches(). Must be called after
  // a call to SecureEraseFile. Files are only securely deleted if DropCaches
  // returns true.
  virtual bool DropCaches();

  // Determines if the given device (under `dev_`) is backed by a rotational
  // hard drive.
  // Returns true if it can conclusively determine it's rotational,
  // otherwise false.
  bool IsRotational(const base::FilePath& device_path);

  void SetDevForTest(const base::FilePath& dev_path) { dev_ = dev_path; }

  void SetSysForTest(const base::FilePath& sys_path) { sys_ = sys_path; }

 protected:
  // These functions are marked protected so they can be overridden for tests.

  // Wrapper around stat(2).
  virtual int Stat(const base::FilePath& path, struct stat* st);

 private:
  ClobberUi* ui_;
  PartitionNumbers partitions_;
  bool fast_wipe_ = false;
  base::FilePath dev_;
  base::FilePath sys_;
};

#endif  // INIT_CLOBBER_CLOBBER_WIPE_H_
