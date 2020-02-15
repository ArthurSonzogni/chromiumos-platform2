// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/minijailed_process_runner.h"

#include <linux/capability.h>

#include <brillo/minijail/mock_minijail.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/network/net_util.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;

namespace arc_networkd {

class MinijailProcessRunnerTest : public testing::Test {
 protected:
  MinijailProcessRunnerTest() : runner_(&mj_) {}

  void SetUp() override {
    ON_CALL(mj_, DropRoot(_, _, _)).WillByDefault(Return(true));
    ON_CALL(mj_, RunSyncAndDestroy(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(0), Return(true)));
  }

  brillo::MockMinijail mj_;
  MinijailedProcessRunner runner_;
};

// Special matcher needed for vector<char*> type.
// Lifted from shill/process_manager_test.cc
MATCHER_P2(IsProcessArgs, program, args, "") {
  if (std::string(arg[0]) != program) {
    return false;
  }
  int index = 1;
  for (const auto& option : args) {
    if (std::string(arg[index++]) != option) {
      return false;
    }
  }
  return arg[index] == nullptr;
}

TEST_F(MinijailProcessRunnerTest, AddInterfaceToContainer) {
  const std::vector<std::string> args_ip1 = {
      "-t", "12345", "-n", "--", "/bin/ip", "link", "set", "foo", "name", "bar",
  };
  const std::vector<std::string> args_ip2 = {
      "-t",  "12345",      "-n",  "--",      "/bin/ip", "addr",
      "add", "1.1.1.1/30", "brd", "1.1.1.3", "dev",     "bar",
  };
  const std::vector<std::string> args_ip3 = {
      "-t", "12345", "-n", "--", "/bin/ip", "link", "set", "bar", "up",
  };
  const std::vector<std::string> args_ip4 = {
      "-t",  "12345", "-n",  "--",        "/bin/ip", "link",
      "set", "dev",   "bar", "multicast", "on",
  };
  EXPECT_CALL(mj_, New()).Times(4);
  EXPECT_CALL(mj_, DropRoot(_, _, _)).Times(0);
  EXPECT_CALL(mj_, RunSyncAndDestroy(
                       _, IsProcessArgs("/usr/bin/nsenter", args_ip1), _));
  EXPECT_CALL(mj_, RunSyncAndDestroy(
                       _, IsProcessArgs("/usr/bin/nsenter", args_ip2), _));
  EXPECT_CALL(mj_, RunSyncAndDestroy(
                       _, IsProcessArgs("/usr/bin/nsenter", args_ip3), _));
  EXPECT_CALL(mj_, RunSyncAndDestroy(
                       _, IsProcessArgs("/usr/bin/nsenter", args_ip4), _));
  runner_.AddInterfaceToContainer("foo", "bar", Ipv4Addr(1, 1, 1, 1), 30, true,
                                  "12345");
}

TEST_F(MinijailProcessRunnerTest, AddInterfaceToContainerNoMulticast) {
  const std::vector<std::string> args_ip1 = {
      "-t", "12345", "-n", "--", "/bin/ip", "link", "set", "foo", "name", "bar",
  };
  const std::vector<std::string> args_ip2 = {
      "-t",  "12345",      "-n",  "--",      "/bin/ip", "addr",
      "add", "1.1.1.1/30", "brd", "1.1.1.3", "dev",     "bar",
  };
  const std::vector<std::string> args_ip3 = {
      "-t", "12345", "-n", "--", "/bin/ip", "link", "set", "bar", "up",
  };
  EXPECT_CALL(mj_, New()).Times(3);
  EXPECT_CALL(mj_, DropRoot(_, _, _)).Times(0);
  EXPECT_CALL(mj_, RunSyncAndDestroy(
                       _, IsProcessArgs("/usr/bin/nsenter", args_ip1), _));
  EXPECT_CALL(mj_, RunSyncAndDestroy(
                       _, IsProcessArgs("/usr/bin/nsenter", args_ip2), _));
  EXPECT_CALL(mj_, RunSyncAndDestroy(
                       _, IsProcessArgs("/usr/bin/nsenter", args_ip3), _));
  runner_.AddInterfaceToContainer("foo", "bar", Ipv4Addr(1, 1, 1, 1), 30, false,
                                  "12345");
}

TEST_F(MinijailProcessRunnerTest, WriteSentinelToContainer) {
  const std::vector<std::string> args = {
      "-t",
      "12345",
      "--mount",
      "--pid",
      "--",
      "/system/bin/touch",
      "/dev/.arc_network_ready",
  };
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, _, _)).Times(0);
  EXPECT_CALL(mj_,
              RunSyncAndDestroy(_, IsProcessArgs("/usr/bin/nsenter", args), _));
  runner_.WriteSentinelToContainer("12345");
}

TEST_F(MinijailProcessRunnerTest, modprobe_all) {
  uint64_t caps = CAP_TO_MASK(CAP_SYS_MODULE);

  const std::vector<std::string> args = {"-a", "module1", "module2"};
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(mj_,
              RunSyncAndDestroy(_, IsProcessArgs("/sbin/modprobe", args), _));
  runner_.modprobe_all({"module1", "module2"});
}

TEST_F(MinijailProcessRunnerTest, sysctl_w) {
  const std::vector<std::string> args = {"-w", "a.b.c=1"};
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_,
              RunSyncAndDestroy(_, IsProcessArgs("/usr/sbin/sysctl", args), _));
  runner_.sysctl_w("a.b.c", "1");
}

TEST_F(MinijailProcessRunnerTest, chown) {
  uint64_t caps = CAP_TO_MASK(CAP_CHOWN);

  const std::vector<std::string> args = {"12345:23456", "foo"};
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(mj_, RunSyncAndDestroy(_, IsProcessArgs("/bin/chown", args), _));
  runner_.chown("12345", "23456", "foo");
}

TEST_F(MinijailProcessRunnerTest, brctl) {
  uint64_t caps = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  const std::vector<std::string> args = {"cmd", "arg", "arg"};

  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(mj_, RunSyncAndDestroy(_, IsProcessArgs("/sbin/brctl", args), _));
  runner_.brctl("cmd", {"arg", "arg"});
}

TEST_F(MinijailProcessRunnerTest, ip) {
  uint64_t caps = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  const std::vector<std::string> args = {"obj", "cmd", "arg", "arg"};

  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(mj_, RunSyncAndDestroy(_, IsProcessArgs("/bin/ip", args), _));
  runner_.ip("obj", "cmd", {"arg", "arg"});
}

TEST_F(MinijailProcessRunnerTest, ip6) {
  uint64_t caps = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  const std::vector<std::string> args = {"-6", "obj", "cmd", "arg", "arg"};

  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(mj_, RunSyncAndDestroy(_, IsProcessArgs("/bin/ip", args), _));
  runner_.ip6("obj", "cmd", {"arg", "arg"});
}

TEST_F(MinijailProcessRunnerTest, iptables) {
  const std::vector<std::string> args = {"-t", "table", "arg", "arg"};

  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_,
              RunSyncAndDestroy(_, IsProcessArgs("/sbin/iptables", args), _));
  runner_.iptables("table", {"arg", "arg"});
}

TEST_F(MinijailProcessRunnerTest, ip6tables) {
  const std::vector<std::string> args = {"-t", "table", "arg", "arg"};

  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_,
              RunSyncAndDestroy(_, IsProcessArgs("/sbin/ip6tables", args), _));
  runner_.ip6tables("table", {"arg", "arg"});
}

}  // namespace arc_networkd
