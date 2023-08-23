// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_FBPREPROCESSOR_DAEMON_H_
#define FBPREPROCESSOR_FBPREPROCESSOR_DAEMON_H_

#include <brillo/daemons/daemon.h>

namespace fbpreprocessor {

class FbPreprocessorDaemon : public brillo::Daemon {
 public:
  FbPreprocessorDaemon();
  FbPreprocessorDaemon(const FbPreprocessorDaemon&) = delete;
  FbPreprocessorDaemon& operator=(const FbPreprocessorDaemon&) = delete;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_FBPREPROCESSOR_DAEMON_H_
