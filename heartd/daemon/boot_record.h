// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_BOOT_RECORD_H_
#define HEARTD_DAEMON_BOOT_RECORD_H_

#include <string>

#include <base/time/time.h>

namespace heartd {

struct BootRecord {
  std::string id;
  base::Time time;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_BOOT_RECORD_H_
