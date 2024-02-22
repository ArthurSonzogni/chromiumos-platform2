// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tc_process.h"

#include <sys/socket.h>

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <net-base/mock_process_manager.h>
#include <net-base/byte_utils.h>
#include <net-base/process_manager.h>
#include <net-base/socket.h>

using testing::_;
using testing::AllOf;
using testing::Return;
using testing::WithArg;
using testing::WithArgs;

namespace shill {
namespace {

const std::vector<std::string> kTCCommands = {"hello\n", "world\n"};
constexpr pid_t kValidPid = 5;

class Client {
 public:
  MOCK_METHOD(void, OnTCProcessExited, (int));
};

TEST(TCProcessTest, CreateSuccess) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::MainThreadType::IO};
  constexpr uint64_t kExpectedCapMask = CAP_TO_MASK(CAP_NET_ADMIN);

  // Use a socket pair to simulate the TCProcess class writes commands to stdin
  // of tc process. fds[1] will be passed to TCProcess and be closed by it.
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_RAW, 0, fds), 0);
  std::unique_ptr<net_base::Socket> read_fd =
      net_base::Socket::CreateFromFd(base::ScopedFD(fds[0]));

  net_base::MockProcessManager mock_process_manager;
  net_base::ProcessManager::ExitCallback exit_callback;
  EXPECT_CALL(
      mock_process_manager,
      StartProcessInMinijailWithPipes(
          _, base::FilePath(TCProcess::kTCPath), _, _,
          AllOf(net_base::MinijailOptionsMatchUserGroup(TCProcess::kTCUser,
                                                        TCProcess::kTCGroup),
                net_base::MinijailOptionsMatchCapMask(kExpectedCapMask)),
          _, _))
      .WillOnce(
          WithArgs<5, 6>([&](net_base::ProcessManager::ExitCallback callback,
                             struct net_base::std_file_descriptors std_fds) {
            *std_fds.stdin_fd = fds[1];
            exit_callback = std::move(callback);
            return kValidPid;
          }));

  Client client;
  std::unique_ptr<TCProcess> tc_process = TCProcess::Create(
      kTCCommands,
      base::BindOnce(&Client::OnTCProcessExited, base::Unretained(&client)),
      &mock_process_manager);
  EXPECT_NE(tc_process, nullptr);

  // Verify the TCProcess writes message into stdin.
  task_environment.RunUntilIdle();
  std::vector<uint8_t> buf;
  read_fd->RecvMessage(&buf);
  EXPECT_EQ(net_base::byte_utils::ByteStringFromBytes(buf), kTCCommands[0]);
  read_fd->RecvMessage(&buf);
  EXPECT_EQ(net_base::byte_utils::ByteStringFromBytes(buf), kTCCommands[1]);

  // Verify the TCProcess send the callback when the process exits, and verify
  // it's ok to destroy the TCProcess instance inside callback.
  EXPECT_CALL(client, OnTCProcessExited(0)).WillOnce([&tc_process]() {
    tc_process.reset();
  });
  std::move(exit_callback).Run(0);
}

TEST(TCProcessTest, FailedToSpawnProcess) {
  net_base::MockProcessManager mock_process_manager;
  EXPECT_CALL(mock_process_manager, StartProcessInMinijailWithPipes)
      .WillOnce(Return(net_base::ProcessManager::kInvalidPID));

  std::unique_ptr<TCProcess> tc_process =
      TCProcess::Create(kTCCommands, base::DoNothing(), &mock_process_manager);
  EXPECT_EQ(tc_process, nullptr);
}

TEST(TCProcessTest, StopProcessAtDestructor) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::MainThreadType::IO};

  // Use a socket pair to simulate the TCProcess class writes commands to stdin
  // of tc process. fds[1] will be passed to TCProcess and be closed by it.
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_RAW, 0, fds), 0);
  std::unique_ptr<net_base::Socket> read_fd =
      net_base::Socket::CreateFromFd(base::ScopedFD(fds[0]));

  net_base::MockProcessManager mock_process_manager;
  EXPECT_CALL(mock_process_manager, StartProcessInMinijailWithPipes)
      .WillOnce(WithArg<6>([&](struct net_base::std_file_descriptors std_fds) {
        *std_fds.stdin_fd = fds[1];
        return kValidPid;
      }));
  std::unique_ptr<TCProcess> tc_process =
      TCProcess::Create(kTCCommands, base::DoNothing(), &mock_process_manager);
  EXPECT_NE(tc_process, nullptr);

  // If the TCProcess is destroyed before the process is exited, then call
  // ProcessManager::StopProcess() to stop the process.
  EXPECT_CALL(mock_process_manager, StopProcess(kValidPid)).Times(1);
  tc_process.reset();
}

}  // namespace
}  // namespace shill
