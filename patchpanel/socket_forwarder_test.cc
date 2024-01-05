// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/socket_forwarder.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <memory>
#include <optional>
#include <vector>

#include <base/functional/callback.h>
#include <base/test/test_future.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/message_loops/base_message_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/socket.h>

using testing::Each;

namespace patchpanel {
namespace {
// SocketForwarder reads blocks of 4096 bytes.
constexpr size_t kDataSize = 5000;

// Does a blocking read on |socket| until it fills up the |buf|.
bool Read(const net_base::Socket& socket, base::span<char> buf) {
  while (buf.size() > 0) {
    const std::optional<size_t> bytes = socket.RecvFrom(buf);
    if (!bytes.has_value()) {
      return false;
    }
    buf = buf.subspan(*bytes);
  }
  return true;
}

}  // namespace

class SocketForwarderTest : public ::testing::Test {
  void SetUp() override {
    int fds0[2], fds1[2];
    ASSERT_NE(-1, socketpair(AF_UNIX, SOCK_STREAM, 0 /* protocol */, fds0));
    ASSERT_NE(-1, socketpair(AF_UNIX, SOCK_STREAM, 0 /* protocol */, fds1));
    peer0_ = net_base::Socket::CreateFromFd(base::ScopedFD(fds0[0]));
    peer1_ = net_base::Socket::CreateFromFd(base::ScopedFD(fds1[0]));
    forwarder_ = std::make_unique<SocketForwarder>(
        "test", net_base::Socket::CreateFromFd(base::ScopedFD(fds0[1])),
        net_base::Socket::CreateFromFd((base::ScopedFD(fds1[1]))));
  }

 protected:
  std::unique_ptr<net_base::Socket> peer0_;
  std::unique_ptr<net_base::Socket> peer1_;
  // Forwards data between |peer0_| and |peer1_|.
  std::unique_ptr<SocketForwarder> forwarder_;

  base::SingleThreadTaskExecutor task_executor_{base::MessagePumpType::IO};
  brillo::BaseMessageLoop brillo_loop_{task_executor_.task_runner()};
};

TEST_F(SocketForwarderTest, ForwardDataAndClose) {
  base::test::TestFuture<void> signal;
  forwarder_->SetStopQuitClosureForTesting(signal.GetCallback());
  forwarder_->Start();

  std::vector<char> msg(kDataSize, 1);

  EXPECT_EQ(peer0_->Send(msg), kDataSize);
  EXPECT_EQ(peer1_->Send(msg), kDataSize);
  // Close both sockets for writing.
  EXPECT_NE(shutdown(peer0_->Get(), SHUT_WR), -1);
  EXPECT_NE(shutdown(peer1_->Get(), SHUT_WR), -1);

  EXPECT_TRUE(signal.Wait());

  EXPECT_FALSE(forwarder_->IsRunning());

  // Verify that all the data has been forwarded to the peers.
  std::vector<char> expected_data_peer0(kDataSize);
  std::vector<char> expected_data_peer1(kDataSize);
  EXPECT_TRUE(Read(*peer1_, expected_data_peer1));
  EXPECT_TRUE(Read(*peer0_, expected_data_peer0));

  EXPECT_THAT(expected_data_peer0, Each(1));
  EXPECT_THAT(expected_data_peer1, Each(1));
}

TEST_F(SocketForwarderTest, PeerSignalEPOLLHUP) {
  base::test::TestFuture<void> signal;
  forwarder_->SetStopQuitClosureForTesting(signal.GetCallback());
  forwarder_->Start();

  // Close the destination peer.
  peer1_.reset();

  EXPECT_TRUE(signal.Wait());

  EXPECT_FALSE(forwarder_->IsRunning());
}

}  // namespace patchpanel
