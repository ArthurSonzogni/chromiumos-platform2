// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_DBUS_FILE_DESCRIPTOR_H_
#define LIBBRILLO_BRILLO_DBUS_FILE_DESCRIPTOR_H_

#include <utility>

#include <base/files/scoped_file.h>

namespace brillo {
namespace dbus_utils {

// TODO(crbug.com/1330447): Repalce this by base::ScopedFD.
// This struct wraps file descriptors to give them a type other than int.
struct FileDescriptor {
  FileDescriptor() = default;
  FileDescriptor(FileDescriptor&& other) : fd(std::move(other.fd)) {}
  FileDescriptor(base::ScopedFD&& other) : fd(std::move(other)) {}  // NOLINT
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  FileDescriptor& operator=(FileDescriptor&& other) {
    fd = std::move(other.fd);
    return *this;
  }

  FileDescriptor& operator=(base::ScopedFD&& other) {
    fd = std::move(other);
    return *this;
  }

  int release() { return fd.release(); }

  int get() const { return fd.get(); }

 private:
  base::ScopedFD fd;
};

}  // namespace dbus_utils
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_DBUS_FILE_DESCRIPTOR_H_
