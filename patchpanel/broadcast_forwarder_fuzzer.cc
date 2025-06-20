// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/broadcast_forwarder.h"

#include <net/if.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/test/task_environment.h>
#include <base/test/test_timeouts.h>
#include <chromeos/net-base/rtnl_message.h>
#include <fuzzer/FuzzedDataProvider.h>

namespace patchpanel {

namespace {

// Test class that overrides BroadcastForwader's sending and receive functions
// with stubs.
class TestBroadcastForwarder : public BroadcastForwarder {
 public:
  explicit TestBroadcastForwarder(const std::string& lan_ifname)
      : BroadcastForwarder(lan_ifname) {}
  TestBroadcastForwarder(const TestBroadcastForwarder&) = delete;
  TestBroadcastForwarder& operator=(const TestBroadcastForwarder&) = delete;
  ~TestBroadcastForwarder() override = default;

  std::unique_ptr<net_base::Socket> Bind(std::string_view ifname,
                                         uint16_t port) override {
    std::unique_ptr<net_base::Socket> socket =
        net_base::Socket::Create(AF_INET, SOCK_DGRAM);
    fds.push_back(socket->Get());
    return socket;
  }

  std::unique_ptr<net_base::Socket> BindRaw(std::string_view ifname) override {
    return Bind(ifname, 0);
  }

  std::unique_ptr<SocketWithIPv4Addr> CreateSocket(
      std::unique_ptr<net_base::Socket> socket,
      const net_base::IPv4Address& addr,
      const net_base::IPv4Address& broadaddr,
      const net_base::IPv4Address& netmask) override {
    return std::make_unique<SocketWithIPv4Addr>(std::move(socket), addr,
                                                broadaddr, netmask);
  }

  ssize_t ReceiveMessage(int fd, struct msghdr* msg) override {
    size_t msg_len = std::min(payload.size(), msg->msg_iov->iov_len);
    if (msg_len > 0) {
      memcpy(msg->msg_iov->iov_base, payload.data(), msg_len);
    }
    return static_cast<ssize_t>(msg_len);
  }

  ssize_t SendTo(int fd,
                 const void* buffer,
                 size_t buffer_len,
                 const struct sockaddr_in* dst_addr) override {
    return static_cast<ssize_t>(buffer_len);
  }

  std::vector<int> fds;
  std::vector<uint8_t> payload;
};

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // <- DISABLE LOGGING.
    base::CommandLine::Init(0, nullptr);
    TestTimeouts::Initialize();
  }
  base::AtExitManager at_exit;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};

  FuzzedDataProvider provider(data, size);
  std::string lan_ifname = provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
  std::string guest_ifname1 = provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
  std::string guest_ifname2 = provider.ConsumeRandomLengthString(IFNAMSIZ - 1);

  TestBroadcastForwarder bcast_forwarder(lan_ifname);
  bcast_forwarder.AddGuest(guest_ifname1);
  bcast_forwarder.AddGuest(guest_ifname2);

  uint64_t fd_index = provider.ConsumeIntegralInRange<uint64_t>(
      0, bcast_forwarder.fds.size() - 1);
  int fd = bcast_forwarder.fds[fd_index];
  bcast_forwarder.payload = provider.ConsumeRemainingBytes<uint8_t>();
  bcast_forwarder.OnFileCanReadWithoutBlocking(fd);

  const auto rtnl_msg = net_base::RTNLMessage::Decode(
      {bcast_forwarder.payload.data(), bcast_forwarder.payload.size()});
  if (rtnl_msg) {
    bcast_forwarder.AddrMsgHandler(*rtnl_msg);
  }

  return 0;
}

}  // namespace
}  // namespace patchpanel
