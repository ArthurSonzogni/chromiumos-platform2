// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/minijailed_process_runner.h"

#include <brillo/minijail/minijail.h>
#include <linux/capability.h>
#include <sys/types.h>

#include <memory>

#include <base/memory/ptr_util.h>
#include <brillo/minijail/mock_minijail.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/iptables.h"
#include "patchpanel/system.h"

using testing::_;
using testing::DoAll;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::Eq;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;
using testing::WithArgs;

namespace patchpanel {
namespace {

constexpr int kFakePid = 123;

class MockSystem : public System {
 public:
  MOCK_METHOD3(WaitPid, pid_t(pid_t pid, int* wstatus, int options));
};

class MinijailProcessRunnerTest : public ::testing::Test {
 protected:
  MinijailProcessRunnerTest() {
    system_ = new MockSystem();
    runner_ = std::make_unique<MinijailedProcessRunner>(
        &mj_, base::WrapUnique(system_));

    jail_ = brillo::Minijail::GetInstance()->New();

    // The default actions are:
    // 1) Set pid to kFakePid;
    // 2) The caller requests pipes for stdout and stderr, create a pipe for
    //    each of them, passing one end to the caller and close the other side
    //    directly (to simulate that the child process closes them and exits).
    // 3) Return true.
    ON_CALL(mj_, RunPipesAndDestroy)
        .WillByDefault(WithArgs<2, 4, 5>(
            [](pid_t* pid, int* stdout_p, int* stderr_p) -> bool {
              *pid = kFakePid;
              int pipe_fds[2] = {};
              if (stdout_p) {
                CHECK_EQ(pipe(pipe_fds), 0);
                *stdout_p = pipe_fds[0];
                close(pipe_fds[1]);
              }
              if (stderr_p) {
                CHECK_EQ(pipe(pipe_fds), 0);
                *stderr_p = pipe_fds[0];
                close(pipe_fds[1]);
              }
              return true;
            }));

    // This is required to prevent a Segmentation Fault when
    // minijail_inherit_usergroups(jail) is invoked within ip() and ip6().
    ON_CALL(mj_, New).WillByDefault(Return(jail_));
  }

  ~MinijailProcessRunnerTest() { minijail_destroy(jail_); }

  brillo::MockMinijail mj_;
  std::unique_ptr<MinijailedProcessRunner> runner_;
  minijail* jail_;
  MockSystem* system_;  // Owned by |runner_|.
};

TEST_F(MinijailProcessRunnerTest, modprobe_all) {
  uint64_t caps = CAP_TO_MASK(CAP_SYS_MODULE);
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")))
      .WillOnce(Return(true));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(mj_, RunPipesAndDestroy(
                       _,
                       ElementsAre(StrEq("/sbin/modprobe"), StrEq("-a"),
                                   StrEq("module1"), StrEq("module2"), nullptr),
                       _, nullptr, _, _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(runner_->modprobe_all({"module1", "module2"}));
}

TEST_F(MinijailProcessRunnerTest, ip) {
  uint64_t caps = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")))
      .WillOnce(Return(true));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(mj_, RunPipesAndDestroy(
                       _,
                       ElementsAre(StrEq("/bin/ip"), StrEq("obj"), StrEq("cmd"),
                                   StrEq("arg1"), StrEq("arg2"), nullptr),
                       _, nullptr, _, _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(runner_->ip("obj", "cmd", {"arg1", "arg2"}));
}

TEST_F(MinijailProcessRunnerTest, ip6) {
  uint64_t caps = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")))
      .WillOnce(Return(true));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(
      mj_, RunPipesAndDestroy(
               _,
               ElementsAre(StrEq("/bin/ip"), StrEq("-6"), StrEq("obj"),
                           StrEq("cmd"), StrEq("arg1"), StrEq("arg2"), nullptr),
               _, nullptr, _, _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(runner_->ip6("obj", "cmd", {"arg1", "arg2"}));
}

TEST_F(MinijailProcessRunnerTest, RunIPAsPatchpanel) {
  uint64_t caps = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, StrEq("patchpaneld"), StrEq("patchpaneld")))
      .WillOnce(Return(true));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(mj_, RunPipesAndDestroy(
                       _,
                       ElementsAre(StrEq("/bin/ip"), StrEq("obj"), StrEq("cmd"),
                                   StrEq("arg1"), StrEq("arg2"), nullptr),
                       _, nullptr, _, _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(runner_->ip("obj", "cmd", {"arg1", "arg2"}, true));
}

TEST_F(MinijailProcessRunnerTest, iptables) {
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(mj_, UseCapabilities(_, _));
  EXPECT_CALL(mj_, RunPipesAndDestroy(
                       _,
                       ElementsAre(StrEq("/sbin/iptables"), StrEq("-t"),
                                   StrEq("filter"), StrEq("-A"), StrEq("chain"),
                                   StrEq("arg1"), StrEq("arg2"), nullptr),
                       _, nullptr, _, _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(runner_->iptables(Iptables::Table::kFilter, Iptables::Command::kA,
                                "chain", {"arg1", "arg2"}));
}

TEST_F(MinijailProcessRunnerTest, ip6tables) {
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(mj_, UseCapabilities(_, _));
  EXPECT_CALL(mj_, RunPipesAndDestroy(
                       _,
                       ElementsAre(StrEq("/sbin/ip6tables"), StrEq("-t"),
                                   StrEq("mangle"), StrEq("-I"), StrEq("chain"),
                                   StrEq("arg1"), StrEq("arg2"), nullptr),
                       _, nullptr, _, _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(runner_->ip6tables(Iptables::Table::kMangle,
                                 Iptables::Command::kI, "chain",
                                 {"arg1", "arg2"}));
}

TEST_F(MinijailProcessRunnerTest, conntrack) {
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, DropRoot(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(mj_, UseCapabilities(_, _));
  EXPECT_CALL(
      mj_, RunPipesAndDestroy(
               _,
               ElementsAre(StrEq("/usr/sbin/conntrack"), StrEq("-U"),
                           StrEq("-p"), StrEq("TCP"), StrEq("-s"),
                           StrEq("8.8.8.8"), StrEq("-m"), StrEq("1"), nullptr),
               _, nullptr, _, _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(
      runner_->conntrack("-U", {"-p", "TCP", "-s", "8.8.8.8", "-m", "1"}));
}
}  // namespace
}  // namespace patchpanel
