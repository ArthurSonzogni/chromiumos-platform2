// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_IP_HELPER_H_
#define ARC_NETWORK_IP_HELPER_H_

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <brillo/daemons/daemon.h>

#include "arc/network/arc_helper.h"
#include "arc/network/ipc.pb.h"

using MessageLoopForIO = base::MessageLoopForIO;

namespace arc_networkd {

// Main loop for the IP helper process.
// This object is used in the subprocess.
class IpHelper : public brillo::Daemon, public base::MessageLoopForIO::Watcher {
 public:
  explicit IpHelper(base::ScopedFD control_fd);

 protected:
  // Overrides Daemon init callback. Returns 0 on success and < 0 on error.
  int OnInit() override;

  // Overrides MessageLoopForIO callbacks for new data on |control_fd_|.
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override {}

  void InitialSetup();

  // Callback from system to notify that a signal was received
  // indicating that ARC is either booting up or going down.
  bool OnSignal(const struct signalfd_siginfo& info);

 private:
  base::ScopedFD control_fd_;
  MessageLoopForIO::FileDescriptorWatcher control_watcher_;
  std::unique_ptr<ArcHelper> arc_helper_;

  base::WeakPtrFactory<IpHelper> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(IpHelper);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_IP_HELPER_H_
