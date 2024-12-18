// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/minijailed_process_runner.h"

#include <linux/capability.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/memory/ptr_util.h>
#include <brillo/minijail/minijail.h>
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
using testing::NiceMock;
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

class ProcessRunnerForTesting : public MinijailedProcessRunner {
 public:
  ProcessRunnerForTesting(brillo::Minijail* mj, std::unique_ptr<System> system)
      : MinijailedProcessRunner(mj, std::move(system)) {}

  static std::vector<char*> CallStringViewToCstrings(
      base::span<std::string_view> argv, base::span<char> memory) {
    return MinijailedProcessRunner::StringViewToCstrings(argv, memory);
  }

 private:
  void UseIptablesSeccompFilter(minijail* jail) override {}
};

class MinijailProcessRunnerTest : public ::testing::Test {
 protected:
  MinijailProcessRunnerTest() {
    system_ = new NiceMock<MockSystem>();
    runner_ = std::make_unique<ProcessRunnerForTesting>(
        &mj_, base::WrapUnique(system_));

    jail_ = brillo::Minijail::GetInstance()->New();

    ON_CALL(mj_, RunAndDestroy)
        .WillByDefault(DoAll(SetArgPointee<2>(kFakePid), Return(true)));

    // This is required to prevent a Segmentation Fault when
    // minijail_inherit_usergroups(jail) is invoked within ip() and ip6().
    ON_CALL(mj_, New).WillByDefault(Return(jail_));
  }

  ~MinijailProcessRunnerTest() { minijail_destroy(jail_); }

  NiceMock<brillo::MockMinijail> mj_;
  std::unique_ptr<MinijailedProcessRunner> runner_;
  minijail* jail_;
  MockSystem* system_;  // Owned by |runner_|.
};

TEST_F(MinijailProcessRunnerTest, modprobe_all) {
  uint64_t caps = CAP_TO_MASK(CAP_SYS_MODULE);
  EXPECT_CALL(mj_, New);
  EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")))
      .WillOnce(Return(true));
  EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
  EXPECT_CALL(mj_, RunAndDestroy(
                       _,
                       ElementsAre(StrEq("/usr/bin/modprobe"), StrEq("-a"),
                                   StrEq("module1"), StrEq("module2"), nullptr),
                       _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  std::vector<std::string> mods = {"module1", "module2"};
  EXPECT_TRUE(runner_->modprobe_all(mods));
}

TEST_F(MinijailProcessRunnerTest, ip) {
  auto set_expectations_for_ip = [this]() -> void {
    uint64_t caps = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
    EXPECT_CALL(mj_, New);
    EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")))
        .WillOnce(Return(true));
    EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
    EXPECT_CALL(mj_, RunAndDestroy(_,
                                   ElementsAre(StrEq("/bin/ip"), StrEq("obj"),
                                               StrEq("cmd"), StrEq("arg1"),
                                               StrEq("arg2"), nullptr),
                                   _));
    EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));
  };

  // Check ip() can be called with initializer list only contains string
  // literals.
  set_expectations_for_ip();
  EXPECT_TRUE(runner_->ip("obj", "cmd", {"arg1", "arg2"}));

  // Check ip() can be called with string vector.
  std::vector<std::string> str_vec_args = {"arg1", "arg2"};
  set_expectations_for_ip();
  EXPECT_TRUE(runner_->ip("obj", "cmd", str_vec_args));

  // Check ip() can be called with string_view vector.
  std::vector<std::string_view> string_view_vector = {"arg1", "arg2"};
  set_expectations_for_ip();
  EXPECT_TRUE(runner_->ip("obj", "cmd", string_view_vector));

  // Check ip() can be called with initializer list that contains string
  // literals and string_view objects.
  set_expectations_for_ip();
  std::string_view arg = "arg1";
  EXPECT_TRUE(runner_->ip("obj", "cmd", {arg, "arg2"}));
}

TEST_F(MinijailProcessRunnerTest, ip6) {
  auto set_expectations_for_ip6 = [this]() -> void {
    uint64_t caps = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
    EXPECT_CALL(mj_, New);
    EXPECT_CALL(mj_, DropRoot(_, StrEq("nobody"), StrEq("nobody")))
        .WillOnce(Return(true));
    EXPECT_CALL(mj_, UseCapabilities(_, Eq(caps)));
    EXPECT_CALL(
        mj_, RunAndDestroy(_,
                           ElementsAre(StrEq("/bin/ip"), StrEq("-6"),
                                       StrEq("obj"), StrEq("cmd"),
                                       StrEq("arg1"), StrEq("arg2"), nullptr),
                           _));
    EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));
  };

  // Check ip6() can be called with initializer list only contains string
  // literals.
  set_expectations_for_ip6();
  EXPECT_TRUE(runner_->ip6("obj", "cmd", {"arg1", "arg2"}));

  // Check ip6() can be called with string vector.
  std::vector<std::string> str_vec_args = {"arg1", "arg2"};
  set_expectations_for_ip6();
  EXPECT_TRUE(runner_->ip6("obj", "cmd", str_vec_args));

  // Check ip6() can be called with string_view vector.
  std::vector<std::string_view> string_view_vector = {"arg1", "arg2"};
  set_expectations_for_ip6();
  EXPECT_TRUE(runner_->ip6("obj", "cmd", string_view_vector));

  // Check ip6() can be called with initializer list that contains string
  // literals and string_view objects.
  set_expectations_for_ip6();
  std::string_view arg = "arg1";
  EXPECT_TRUE(runner_->ip6("obj", "cmd", {arg, "arg2"}));
}

TEST_F(MinijailProcessRunnerTest, RunIPAsPatchpanel) {
  uint64_t caps = CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);
  EXPECT_CALL(mj_, New);
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
  auto set_expectations_for_iptables = [this]() -> void {
    EXPECT_CALL(mj_, New);
    EXPECT_CALL(mj_, UseCapabilities);
    EXPECT_CALL(
        mj_,
        RunAndDestroy(_,
                      ElementsAre(StrEq("/sbin/iptables"), StrEq("-t"),
                                  StrEq("filter"), StrEq("-A"), StrEq("chain"),
                                  StrEq("arg1"), StrEq("arg2"), nullptr),
                      _));
    EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));
  };

  // Check iptables() can be called with initializer list only contains string
  // literals.
  set_expectations_for_iptables();
  EXPECT_TRUE(runner_->iptables(Iptables::Table::kFilter, Iptables::Command::kA,
                                "chain", {"arg1", "arg2"}));
  // Check iptables() can be called with string vector.
  set_expectations_for_iptables();
  std::vector<std::string> str_vec_args = {"arg1", "arg2"};
  EXPECT_TRUE(runner_->iptables(Iptables::Table::kFilter, Iptables::Command::kA,
                                "chain", str_vec_args));

  // Check iptables() can be called with string_view vector.
  std::vector<std::string_view> string_view_vector = {"arg1", "arg2"};
  set_expectations_for_iptables();
  EXPECT_TRUE(runner_->iptables(Iptables::Table::kFilter, Iptables::Command::kA,
                                "chain", string_view_vector));

  // Check iptables() can be called with initializer list that contains string
  // literals and string_view objects.
  set_expectations_for_iptables();
  std::string_view arg = "arg1";
  EXPECT_TRUE(runner_->iptables(Iptables::Table::kFilter, Iptables::Command::kA,
                                "chain", {arg, "arg2"}));
}

TEST_F(MinijailProcessRunnerTest, ip6tables) {
  auto set_expectations_for_ip6tables = [this]() -> void {
    EXPECT_CALL(mj_, New);
    EXPECT_CALL(mj_, UseCapabilities);
    EXPECT_CALL(
        mj_,
        RunAndDestroy(_,
                      ElementsAre(StrEq("/sbin/ip6tables"), StrEq("-t"),
                                  StrEq("filter"), StrEq("-A"), StrEq("chain"),
                                  StrEq("arg1"), StrEq("arg2"), nullptr),
                      _));
    EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
        .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));
  };

  // Check ip6tables() can be called with initializer list only contains string
  // literals.
  set_expectations_for_ip6tables();
  EXPECT_TRUE(runner_->ip6tables(Iptables::Table::kFilter,
                                 Iptables::Command::kA, "chain",
                                 {"arg1", "arg2"}));
  // Check ip6tables() can be called with string vector.
  set_expectations_for_ip6tables();
  std::vector<std::string> str_vec_args = {"arg1", "arg2"};
  EXPECT_TRUE(runner_->ip6tables(Iptables::Table::kFilter,
                                 Iptables::Command::kA, "chain", str_vec_args));

  // Check ip6tables() can be called with string_view vector.
  std::vector<std::string_view> string_view_vector = {"arg1", "arg2"};
  set_expectations_for_ip6tables();
  EXPECT_TRUE(runner_->ip6tables(Iptables::Table::kFilter,
                                 Iptables::Command::kA, "chain",
                                 string_view_vector));

  // Check ip6tables() can be called with initializer list that contains string
  // literals and string_view objects.
  set_expectations_for_ip6tables();
  std::string_view arg = "arg1";
  EXPECT_TRUE(runner_->ip6tables(
      Iptables::Table::kFilter, Iptables::Command::kA, "chain", {arg, "arg2"}));
}

TEST_F(MinijailProcessRunnerTest, conntrack) {
  EXPECT_CALL(mj_, New);
  EXPECT_CALL(mj_, DropRoot(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(mj_, UseCapabilities);
  EXPECT_CALL(
      mj_, RunAndDestroy(
               _,
               ElementsAre(StrEq("/usr/sbin/conntrack"), StrEq("-U"),
                           StrEq("-p"), StrEq("TCP"), StrEq("-s"),
                           StrEq("8.8.8.8"), StrEq("-m"), StrEq("1"), nullptr),
               _));
  EXPECT_CALL(*system_, WaitPid(kFakePid, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(1), Return(kFakePid)));

  std::vector<std::string> args = {"-p", "TCP", "-s", "8.8.8.8", "-m", "1"};
  EXPECT_TRUE(runner_->conntrack("-U", args));
}

class IptablesBatchModeTest : public MinijailProcessRunnerTest {
 protected:
  using Table = Iptables::Table;
  using Command = Iptables::Command;

  void CallIptablesAndSetupExpectation() {
    EXPECT_EQ(
        0, runner_->iptables(Table::kMangle, Command::kN, "chain_4", {"-w"}));
    EXPECT_EQ(0, runner_->iptables(Table::kMangle, Command::kA, "chain_4", {}));
    EXPECT_EQ(0, runner_->iptables(Table::kNat, Command::kF, "chain_4",
                                   {"a", "b", "c"}));
    EXPECT_EQ(0, runner_->iptables(Table::kFilter, Command::kD, "chain_4",
                                   {"a", "b", "-w"}));
    EXPECT_EQ(
        0, runner_->ip6tables(Table::kMangle, Command::kN, "chain_6", {"-w"}));
    EXPECT_EQ(0,
              runner_->ip6tables(Table::kMangle, Command::kA, "chain_6", {}));
    EXPECT_EQ(0, runner_->ip6tables(Table::kNat, Command::kF, "chain_6",
                                    {"a", "c", "-w"}));
    EXPECT_EQ(0, runner_->ip6tables(Table::kFilter, Command::kD, "chain_6",
                                    {"a", "b"}));

    EXPECT_CALL(mj_,
                RunAndDestroy(_,
                              ElementsAre(StrEq("/sbin/iptables-restore"),
                                          StrEq("-n"), _, StrEq("-w"), nullptr),
                              _))
        .WillOnce(WithArgs<1>([&](std::vector<char*> argv) {
          CHECK(base::ReadFileToString(base::FilePath(argv[2]),
                                       &actual_ipv4_input_));
          return true;
        }));
    EXPECT_CALL(mj_,
                RunAndDestroy(_,
                              ElementsAre(StrEq("/sbin/ip6tables-restore"),
                                          StrEq("-n"), _, StrEq("-w"), nullptr),
                              _))
        .WillOnce(WithArgs<1>([&](std::vector<char*> argv) {
          CHECK(base::ReadFileToString(base::FilePath(argv[2]),
                                       &actual_ipv6_input_));
          return true;
        }));
  }

  void VerifyIptablesRestoreInput() {
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

    EXPECT_EQ(actual_ipv4_input_, expected_ipv4_input);
    EXPECT_EQ(actual_ipv6_input_, expected_ipv6_input);
  }

 private:
  std::string actual_ipv4_input_;
  std::string actual_ipv6_input_;
};

// Verifies that iptables-restore is triggered with the expected input.
TEST_F(IptablesBatchModeTest, IptablesBatchModeSuccess) {
  auto batch_mode = runner_->AcquireIptablesBatchMode();
  CallIptablesAndSetupExpectation();
  ASSERT_TRUE(runner_->CommitIptablesRules(std::move(batch_mode)));
  VerifyIptablesRestoreInput();

  // We are no longer in batch mode now. iptables() call should start the
  // iptables process directly.
  EXPECT_CALL(mj_, RunAndDestroy(_, Contains(StrEq("/sbin/iptables")), _));
  runner_->iptables(Table::kMangle, Command::kN, "chain_4", {"-w"});
  EXPECT_CALL(mj_, RunAndDestroy(_, Contains(StrEq("/sbin/ip6tables")), _));
  runner_->ip6tables(Table::kMangle, Command::kN, "chain_6", {"-w"});

  // Enter batch mode again. Make sure that the rules won't be leftover.
  batch_mode = runner_->AcquireIptablesBatchMode();
  CallIptablesAndSetupExpectation();
  batch_mode = nullptr;  // destruct object to trigger the execution
  VerifyIptablesRestoreInput();
}

// Verifies that execution failure can be passed back to the caller.
TEST_F(IptablesBatchModeTest, Failure) {
  auto batch_mode = runner_->AcquireIptablesBatchMode();
  runner_->iptables(Table::kMangle, Command::kN, "chain_4", {"-w"});
  runner_->ip6tables(Table::kMangle, Command::kN, "chain_6", {"-w"});

  EXPECT_CALL(mj_, RunAndDestroy).WillRepeatedly(Return(false));

  ASSERT_FALSE(runner_->CommitIptablesRules(std::move(batch_mode)));
}

// Verifies that batch mode cannot be acquired twice.
TEST_F(IptablesBatchModeTest, AcquireTwice) {
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

TEST_F(MinijailProcessRunnerTest, StringViewToCstrings) {
  std::vector<std::string_view> argv1 = {"arg1", "arg2", "arg3"};

  char buffer[32];
  base::span<char> memory(buffer);
  std::vector<char*> args =
      ProcessRunnerForTesting::CallStringViewToCstrings(argv1, memory);

  EXPECT_EQ(4, args.size());
  EXPECT_STREQ("arg1", args[0]);
  EXPECT_STREQ("arg2", args[1]);
  EXPECT_STREQ("arg3", args[2]);
  EXPECT_EQ(nullptr, args[3]);

  // Test that StringViewToCstrings() can be reused.
  std::vector<std::string_view> argv2 = {"arg4", "arg5"};
  args = ProcessRunnerForTesting::CallStringViewToCstrings(argv2, memory);
  EXPECT_EQ(3, args.size());
  EXPECT_STREQ("arg4", args[0]);
  EXPECT_STREQ("arg5", args[1]);
  EXPECT_EQ(nullptr, args[2]);

  // Test that when args size is larger than buffer size, empty vector is
  // returned.
  char small_buf[1];
  base::span<char> small_memory(small_buf);
  args = ProcessRunnerForTesting::CallStringViewToCstrings(argv1, small_memory);
  EXPECT_EQ(0, args.size());
}

}  // namespace
}  // namespace patchpanel
