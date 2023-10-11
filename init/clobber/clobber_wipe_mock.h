// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_CLOBBER_CLOBBER_WIPE_MOCK_H_
#define INIT_CLOBBER_CLOBBER_WIPE_MOCK_H_

#include <string>
#include <unordered_map>

#include <base/files/file_path.h>
#include <brillo/files/file_util.h>

#include "init/clobber/clobber_ui.h"

// Needed for "mocking UI", redirect to /dev/null
base::File DevNull();

bool CreateDirectoryAndWriteFile(const base::FilePath& path,
                                 const std::string& contents);

// Version of ClobberWipe with some library calls mocked for testing.
class ClobberWipeMock : public ClobberWipe {
 public:
  explicit ClobberWipeMock(ClobberUi* ui)
      : ClobberWipe(ui), secure_erase_supported_(false) {}

  void SetStatResultForPath(const base::FilePath& path, const struct stat& st) {
    result_map_[path.value()] = st;
  }

  void SetSecureEraseSupported(bool supported) {
    secure_erase_supported_ = supported;
  }

  void SetWipeDevice(bool ret) { wipe_device_ret_ = ret; }

  uint64_t WipeDeviceCalled() { return wipe_device_called_; }

 protected:
  int Stat(const base::FilePath& path, struct stat* st) override {
    if (st == nullptr || result_map_.count(path.value()) == 0) {
      return -1;
    }

    *st = result_map_[path.value()];
    return 0;
  }

  bool SecureErase(const base::FilePath& path) override {
    return secure_erase_supported_ && brillo::DeleteFile(path);
  }

  bool DropCaches() override { return secure_erase_supported_; }

  bool WipeDevice(const base::FilePath& device_name,
                  bool discard = false) override {
    ++wipe_device_called_;
    return wipe_device_ret_;
  }

 private:
  std::unordered_map<std::string, struct stat> result_map_;
  bool secure_erase_supported_;

  uint64_t wipe_device_called_ = 0;
  bool wipe_device_ret_ = true;
};

#endif  // INIT_CLOBBER_CLOBBER_WIPE_MOCK_H_
