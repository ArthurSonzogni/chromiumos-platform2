// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_FBPREPROCESSOR_DAEMON_H_
#define FBPREPROCESSOR_FBPREPROCESSOR_DAEMON_H_

#include <memory>

#include <brillo/daemons/daemon.h>
#include <dbus/bus.h>

#include "fbpreprocessor/manager.h"

namespace fbpreprocessor {

class FbPreprocessorDaemon : public brillo::Daemon {
 public:
  FbPreprocessorDaemon();
  FbPreprocessorDaemon(const FbPreprocessorDaemon&) = delete;
  FbPreprocessorDaemon& operator=(const FbPreprocessorDaemon&) = delete;

 private:
  scoped_refptr<dbus::Bus> bus_;

  std::unique_ptr<Manager> manager_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_FBPREPROCESSOR_DAEMON_H_
