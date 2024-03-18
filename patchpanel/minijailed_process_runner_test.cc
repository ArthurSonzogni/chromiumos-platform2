// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/minijailed_process_runner.h"

#include <brillo/minijail/minijail.h>
#include <linux/capability.h>
#include <sys/types.h>

#include <memory>

#include <base/files/file_util.h>
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
    runner_ = MinijailedProcessRunner::CreateForTesting(
        &mj_, base::WrapUnique(system_));

    jail_ = brillo::Minijail::GetInstance()->New();

    ON_CALL(mj_, RunAndDestroy)
        .WillByDefault(DoAll(SetArgPointee<2>(kFakePid), Return(true)));

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
  EXPECT_CALL(mj_, RunAndDestroy(
                       _,
                       ElementsAre(StrEq("/sbin/modprobe"), StrEq("-a"),
                                   StrEq("module1"), StrEq("module2"), nullptr),
                       _));
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
  EXPECT_CALL(mj_, RunAndDestroy(
                       _,
                       ElementsAre(StrEq("/bin/ip"), StrEq("obj"), StrEq("cmd"),
                                   StrEq("arg1"), StrEq("arg2"), nullptr),
                       _));
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
      mj_, RunAndDestroy(
               _,
               ElementsAre(StrEq("/bin/ip"), StrEq("-6"), StrEq("obj"),
                           StrEq("cmd"), StrEq("arg1"), StrEq("arg2"), nullptr),
               _));
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
  EXPECT_CALL(mj_, RunAndDestroy(
                       _,
                       ElementsAre(StrEq("/bin/ip"), StrEq("obj"), StrEq("cmd"),
                                   StrEq("arg1"), StrEq("arg2"), nullptr),
                       _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(runner_->ip("obj", "cmd", {"arg1", "arg2"}, true));
}

TEST_F(MinijailProcessRunnerTest, iptables) {
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, UseCapabilities(_, _));
  EXPECT_CALL(mj_, RunAndDestroy(
                       _,
                       ElementsAre(StrEq("/sbin/iptables"), StrEq("-t"),
                                   StrEq("filter"), StrEq("-A"), StrEq("chain"),
                                   StrEq("arg1"), StrEq("arg2"), nullptr),
                       _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(runner_->iptables(Iptables::Table::kFilter, Iptables::Command::kA,
                                "chain", {"arg1", "arg2"}));
}

TEST_F(MinijailProcessRunnerTest, ip6tables) {
  EXPECT_CALL(mj_, New());
  EXPECT_CALL(mj_, UseCapabilities(_, _));
  EXPECT_CALL(mj_, RunAndDestroy(
                       _,
                       ElementsAre(StrEq("/sbin/ip6tables"), StrEq("-t"),
                                   StrEq("mangle"), StrEq("-I"), StrEq("chain"),
                                   StrEq("arg1"), StrEq("arg2"), nullptr),
                       _));
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
      mj_, RunAndDestroy(
               _,
               ElementsAre(StrEq("/usr/sbin/conntrack"), StrEq("-U"),
                           StrEq("-p"), StrEq("TCP"), StrEq("-s"),
                           StrEq("8.8.8.8"), StrEq("-m"), StrEq("1"), nullptr),
               _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  EXPECT_TRUE(
      runner_->conntrack("-U", {"-p", "TCP", "-s", "8.8.8.8", "-m", "1"}));
}

// Verifies that iptables-restore is triggered with the expected input.
TEST_F(MinijailProcessRunnerTest, IptablesBatchModeSuccess) {
  using Table = Iptables::Table;
  using Command = Iptables::Command;

  bool success = false;
  auto batch_mode = runner_->AcquireIptablesBatchMode(&success);
  EXPECT_EQ(0,
            runner_->iptables(Table::kMangle, Command::kN, "chain_4", {"-w"}));
  EXPECT_EQ(0, runner_->iptables(Table::kMangle, Command::kA, "chain_4", {}));
  EXPECT_EQ(0, runner_->iptables(Table::kNat, Command::kF, "chain_4",
                                 {"a", "b", "c"}));
  EXPECT_EQ(0, runner_->iptables(Table::kFilter, Command::kD, "chain_4",
                                 {"a", "b", "-w"}));
  EXPECT_EQ(0,
            runner_->ip6tables(Table::kMangle, Command::kN, "chain_6", {"-w"}));
  EXPECT_EQ(0, runner_->ip6tables(Table::kMangle, Command::kA, "chain_6", {}));
  EXPECT_EQ(0, runner_->ip6tables(Table::kNat, Command::kF, "chain_6",
                                  {"a", "c", "-w"}));
  EXPECT_EQ(0, runner_->ip6tables(Table::kFilter, Command::kD, "chain_6",
                                  {"a", "b"}));

  std::string_view expected_ipv4_input =
      "*filter\n"
      "-D chain_4 a b\n"
      "COMMIT\n"
      "\n"
      "*mangle\n"
      ":chain_4 - [0:0]\n"
      "-A chain_4\n"
      "COMMIT\n"
      "\n"
      "*nat\n"
      "-F chain_4 a b c\n"
      "COMMIT\n";
  std::string_view expected_ipv6_input =
      "*filter\n"
      "-D chain_6 a b\n"
      "COMMIT\n"
      "\n"
      "*mangle\n"
      ":chain_6 - [0:0]\n"
      "-A chain_6\n"
      "COMMIT\n"
      "\n"
      "*nat\n"
      "-F chain_6 a c\n"
      "COMMIT\n";

  std::string actual_ipv4_input;
  std::string actual_ipv6_input;
  EXPECT_CALL(mj_,
              RunAndDestroy(_,
                            ElementsAre(StrEq("/sbin/iptables-restore"),
                                        StrEq("-n"), _, StrEq("-w"), nullptr),
                            _))
      .WillOnce(WithArgs<1>([&](std::vector<char*> argv) {
        CHECK(base::ReadFileToString(base::FilePath(argv[2]),
                                     &actual_ipv4_input));
        return true;
      }));
  EXPECT_CALL(mj_,
              RunAndDestroy(_,
                            ElementsAre(StrEq("/sbin/ip6tables-restore"),
                                        StrEq("-n"), _, StrEq("-w"), nullptr),
                            _))
      .WillOnce(WithArgs<1>([&](std::vector<char*> argv) {
        CHECK(base::ReadFileToString(base::FilePath(argv[2]),
                                     &actual_ipv6_input));
        return true;
      }));

  // Trigger the execution.
  batch_mode = nullptr;

  EXPECT_TRUE(success);
  EXPECT_EQ(actual_ipv4_input, expected_ipv4_input);
  EXPECT_EQ(actual_ipv6_input, expected_ipv6_input);

  // We are no longer in batch mode now. iptables() call should start the
  // iptables process directly.
  EXPECT_CALL(mj_, RunAndDestroy(_, Contains(StrEq("/sbin/iptables")), _));
  runner_->iptables(Table::kMangle, Command::kN, "chain_4", {"-w"});
  EXPECT_CALL(mj_, RunAndDestroy(_, Contains(StrEq("/sbin/ip6tables")), _));
  runner_->ip6tables(Table::kMangle, Command::kN, "chain_6", {"-w"});

  // Enter batch mode again, do nothing here, no iptables-restore process will
  // be started. This is to make sure that the rules won't be leftover.
  batch_mode = runner_->AcquireIptablesBatchMode(&success);
  EXPECT_CALL(mj_, RunAndDestroy).Times(0);
  batch_mode = nullptr;
}

// Verifies that execution failure can be passed back to the caller.
TEST_F(MinijailProcessRunnerTest, IptablesBatchModeFailure) {
  using Table = Iptables::Table;
  using Command = Iptables::Command;

  bool success = false;
  auto batch_mode = runner_->AcquireIptablesBatchMode(&success);
  runner_->iptables(Table::kMangle, Command::kN, "chain_4", {"-w"});
  runner_->ip6tables(Table::kMangle, Command::kN, "chain_6", {"-w"});

  EXPECT_CALL(mj_, RunAndDestroy).WillRepeatedly(Return(false));

  // Trigger the execution.
  batch_mode = nullptr;

  EXPECT_FALSE(success);
}

// Verifies that batch mode cannot be acquired twice.
TEST_F(MinijailProcessRunnerTest, IptablesBatchModeAcquireTwice) {
  auto batch_mode = runner_->AcquireIptablesBatchMode();
  ASSERT_NE(batch_mode.get(), nullptr);
  ASSERT_EQ(runner_->AcquireIptablesBatchMode(), nullptr);
}

TEST_F(MinijailProcessRunnerTest, IptablesBatchModeInvalidInput) {
  auto batch_mode = runner_->AcquireIptablesBatchMode();

  constexpr auto kTable = Iptables::Table::kMangle;
  constexpr auto kCmd = Iptables::Command::kN;

  EXPECT_EQ(-1, runner_->iptables(kTable, kCmd, "invalid_chain\n", {}));
  EXPECT_EQ(-1, runner_->iptables(kTable, kCmd, "chain", {"abc\n"}));
  EXPECT_EQ(-1, runner_->iptables(kTable, kCmd, "chain", {"a bc"}));
  EXPECT_EQ(-1, runner_->iptables(kTable, kCmd, "chain", {"\tabc"}));
  EXPECT_EQ(-1, runner_->iptables(kTable, kCmd, "chain", {"\""}));
  EXPECT_EQ(-1, runner_->iptables(kTable, kCmd, "chain", {"\'"}));

  EXPECT_EQ(-1, runner_->ip6tables(kTable, kCmd, "chain", {"abc\n"}));
  EXPECT_EQ(-1, runner_->ip6tables(kTable, kCmd, "chain", {"a bc"}));
  EXPECT_EQ(-1, runner_->ip6tables(kTable, kCmd, "chain", {"\tabc"}));
  EXPECT_EQ(-1, runner_->ip6tables(kTable, kCmd, "chain", {"\""}));
  EXPECT_EQ(-1, runner_->ip6tables(kTable, kCmd, "chain", {"\'"}));

  // No rule was added.
  EXPECT_CALL(mj_, RunAndDestroy).Times(0);
  batch_mode = nullptr;
}

}  // namespace
}  // namespace patchpanel
