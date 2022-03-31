// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sysexits.h>
#include <unistd.h>

#include <string>

#include <base/check_op.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/posix/eintr_wrapper.h>
#include <base/test/bind.h>
#include <base/test/test_timeouts.h>
#include <base/threading/platform_thread.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/process/process.h>
#include <gtest/gtest.h>

#include "mojo_service_manager/daemon/daemon.h"

namespace chromeos {
namespace mojo_service_manager {
namespace {

constexpr char kTestSocketName[] = "test_socket";

class DaemonTest : public ::testing::Test {
 public:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath GetSocketPath() {
    return temp_dir_.GetPath().Append(kTestSocketName);
  }

  // Starts a daemon in another process for testing.
  bool StartDaemonProcess(const base::FilePath& socket_path) {
    pid_t pid = fork();
    if (pid == -1) {
      PLOG(ERROR) << "Failed to fork for daemon process.";
      return false;
    }
    if (pid == 0) {
      // In child process, run until the daemon stop. Daemon needs to be created
      // in this process.
      Daemon daemon{socket_path, Configuration{}, ServicePolicyMap{}};
      exit(daemon.Run());
    }
    // In parent process, track the daemon process in |daemon_process_|.
    daemon_process_.Reset(pid);
    // Wait for the server to be ready.
    for (base::ElapsedTimer t; t.Elapsed() < TestTimeouts::action_timeout();
         base::PlatformThread::Sleep(base::Milliseconds(1))) {
      if (base::PathExists(socket_path))
        return true;
    }
    LOG(ERROR) << "Socket server didn't show up after timeout.";
    return false;
  }

 private:
  // Temp directory for test.
  base::ScopedTempDir temp_dir_;
  // Keeps the daemon process.
  brillo::ProcessImpl daemon_process_;
};

base::ScopedFD ConnectToSocket(const base::FilePath& socket_path) {
  base::ScopedFD sock{socket(AF_UNIX, SOCK_STREAM, 0)};
  if (!sock.is_valid()) {
    LOG(ERROR) << "Failed to create socket";
    return base::ScopedFD{};
  }

  struct sockaddr_un unix_addr {
    .sun_family = AF_UNIX,
  };
  constexpr size_t kMaxSize =
      sizeof(unix_addr.sun_path) - /*NULL-terminator*/ 1;
  CHECK_LE(socket_path.value().size(), kMaxSize);
  strncpy(unix_addr.sun_path, socket_path.value().c_str(), kMaxSize);

  int rc = HANDLE_EINTR(connect(sock.get(),
                                reinterpret_cast<const sockaddr*>(&unix_addr),
                                sizeof(unix_addr)));
  if (rc == -1 && errno != EISCONN) {
    LOG(ERROR) << "Failed to connect to socket";
    return base::ScopedFD{};
  }
  return sock;
}

TEST_F(DaemonTest, FailToListenSocket) {
  // Create the socket file so the daemon will fail to create it.
  ASSERT_TRUE(base::WriteFile(GetSocketPath(), "test"));
  Daemon daemon{GetSocketPath(), Configuration{}, ServicePolicyMap{}};
  EXPECT_NE(daemon.Run(), EX_OK);
}

TEST_F(DaemonTest, Connect) {
  ASSERT_TRUE(StartDaemonProcess(GetSocketPath()));

  base::ScopedFD sock = ConnectToSocket(GetSocketPath());
  ASSERT_TRUE(sock.is_valid());

  // TODO(chungsheng): Verify the sock can be used after implement the server.
}

}  // namespace
}  // namespace mojo_service_manager
}  // namespace chromeos
