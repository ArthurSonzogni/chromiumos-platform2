// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_DBUS_FILE_DESCRIPTOR_H_
#define LIBBRILLO_BRILLO_DBUS_FILE_DESCRIPTOR_H_

#include <utility>

#include <base/files/scoped_file.h>

namespace brillo {
namespace dbus_utils {

// TODO(crbug.com/1330447): Remove FileDescriptor.
using FileDescriptor = base::ScopedFD;

}  // namespace dbus_utils
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_DBUS_FILE_DESCRIPTOR_H_
