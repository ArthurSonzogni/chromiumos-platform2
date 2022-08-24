// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>

namespace diagnostics {

class DelegateDaemon : public brillo::Daemon {
 public:
  DelegateDaemon();
  DelegateDaemon(const DelegateDaemon&) = delete;
  DelegateDaemon& operator=(const DelegateDaemon&) = delete;
  ~DelegateDaemon();
};

DelegateDaemon::DelegateDaemon() {}
DelegateDaemon::~DelegateDaemon() {}

}  // namespace diagnostics

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);
  base::CommandLine::Init(argc, argv);

  diagnostics::DelegateDaemon daemon;
  return daemon.Run();
}
