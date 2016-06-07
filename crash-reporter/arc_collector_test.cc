// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/arc_collector.h"

#include <memory>
#include <unordered_map>

#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using brillo::ClearLog;
using brillo::FindLog;
using brillo::GetLog;

namespace {

const char kCrashLog[] = R"(
Process: com.arc.app
Flags: 0xcafebabe
Package: com.arc.app v1 (1.0)
Build: fingerprint

Line 1
Line 2
Line 3
)";

}  // namespace

class MockArcCollector : public ArcCollector {
 public:
  using ArcCollector::ArcCollector;
  MOCK_METHOD0(SetUpDBus, void());
};

class Test : public ::testing::Test {
 protected:
  void Initialize() {
    EXPECT_CALL(*collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_->Initialize(CountCrash, IsFeedbackAllowed, false, false, "");
    ClearLog();
  }

  std::unique_ptr<MockArcCollector> collector_;

 private:
  static void CountCrash() {}
  static bool IsFeedbackAllowed() { return true; }
};

class ArcCollectorTest : public Test {
 protected:
  class MockContext : public ArcCollector::Context {
   public:
    void SetArcPid(pid_t pid) { arc_pid_ = pid; }
    void AddProcess(pid_t pid,
                    const char *ns,
                    const char *exe,
                    const char *cmd) {
      DCHECK_EQ(processes_.count(pid), 0u);
      DCHECK(ns);
      DCHECK(exe);
      auto& process = processes_[pid];
      process.ns = ns;
      process.exe = exe;
      process.cmd = cmd;
    }

   private:
    struct Process {
      const char *ns;
      const char *exe;
      const char *cmd;
    };

    bool GetArcPid(pid_t *pid) const override {
      if (arc_pid_ == 0)
        return false;
      *pid = arc_pid_;
      return true;
    }
    bool GetPidNamespace(pid_t pid, std::string *ns) const override {
      const auto it = processes_.find(pid);
      if (it == processes_.end())
        return false;
      ns->assign(it->second.ns);
      return true;
    }
    bool GetExeBaseName(pid_t pid, std::string *exe) const override {
      const auto it = processes_.find(pid);
      if (it == processes_.end())
        return false;
      exe->assign(it->second.exe);
      return true;
    }
    bool GetCommand(pid_t pid, std::string *command) const override {
      const auto it = processes_.find(pid);
      if (it == processes_.end())
        return false;
      const auto cmd = it->second.cmd;
      if (!cmd)
        return false;
      command->assign(cmd);
      return true;
    }

    pid_t arc_pid_ = 0;
    std::unordered_map<pid_t, Process> processes_;
  };

  MockContext *context_;  // Owned by collector.

 private:
  void SetUp() override {
    context_ = new MockContext;
    collector_.reset(new MockArcCollector(ArcCollector::ContextPtr(context_)));
    Initialize();
  }
};

class ArcContextTest : public Test {
 protected:
  pid_t pid_;

 private:
  void SetUp() override {
    collector_.reset(new MockArcCollector);
    Initialize();
    pid_ = getpid();
  }
};

TEST_F(ArcCollectorTest, IsArcProcess) {
  EXPECT_FALSE(collector_->IsArcProcess(123));
  EXPECT_TRUE(FindLog("Failed to get PID of ARC container"));
  ClearLog();

  context_->SetArcPid(100);

  EXPECT_FALSE(collector_->IsArcProcess(123));
  EXPECT_TRUE(FindLog("Failed to get PID namespace of ARC container"));
  ClearLog();

  context_->AddProcess(100, "arc", "init", "/sbin/init");

  EXPECT_FALSE(collector_->IsArcProcess(123));
  EXPECT_TRUE(FindLog("Failed to get PID namespace of process"));
  ClearLog();

  context_->AddProcess(50, "cros", "chrome", "/opt/google/chrome/chrome");
  context_->AddProcess(123, "arc", "arc_service", "/sbin/arc_service");

  EXPECT_TRUE(collector_->IsArcProcess(123));
  EXPECT_TRUE(GetLog().empty());

  EXPECT_FALSE(collector_->IsArcProcess(50));
  EXPECT_TRUE(GetLog().empty());
}

TEST_F(ArcCollectorTest, GetExeBaseNameForUserCrash) {
  context_->SetArcPid(100);
  context_->AddProcess(100, "arc", "init", "/sbin/init");
  context_->AddProcess(50, "cros", "chrome", "/opt/google/chrome/chrome");

  std::string exe;
  EXPECT_TRUE(collector_->GetExecutableBaseNameFromPid(50, &exe));
  EXPECT_EQ("chrome", exe);
}

TEST_F(ArcCollectorTest, GetExeBaseNameForArcCrash) {
  context_->SetArcPid(100);
  context_->AddProcess(100, "arc", "init", "/sbin/init");
  context_->AddProcess(123, "arc", "arc_service", "/sbin/arc_service");
  context_->AddProcess(456, "arc", "app_process32", nullptr);
  context_->AddProcess(789, "arc", "app_process32", "com.arc.app");

  std::string exe;

  EXPECT_TRUE(collector_->GetExecutableBaseNameFromPid(123, &exe));
  EXPECT_EQ("arc_service", exe);

  EXPECT_TRUE(collector_->GetExecutableBaseNameFromPid(456, &exe));
  EXPECT_TRUE(FindLog("Failed to get package name"));
  EXPECT_EQ("app_process32", exe);

  EXPECT_TRUE(collector_->GetExecutableBaseNameFromPid(789, &exe));
  EXPECT_EQ("com.arc.app", exe);
}

TEST_F(ArcCollectorTest, ShouldDump) {
  context_->SetArcPid(100);
  context_->AddProcess(50, "cros", "chrome", "/opt/google/chrome/chrome");
  context_->AddProcess(100, "arc", "init", "/sbin/init");
  context_->AddProcess(123, "arc", "arc_service", "/sbin/arc_service");
  context_->AddProcess(789, "arc", "app_process32", "com.arc.app");

  std::string reason;
  EXPECT_FALSE(collector_->ShouldDump(50, 1234, "chrome", &reason));
  EXPECT_EQ("ignoring - crash origin is not ARC", reason);

  EXPECT_TRUE(collector_->ShouldDump(123, 0, "arc_service", &reason));
  EXPECT_EQ("handling", reason);

  EXPECT_FALSE(collector_->ShouldDump(123, ArcCollector::kSystemUserEnd,
                                      "com.arc.app", &reason));
  EXPECT_EQ("ignoring - not a system process", reason);
}

TEST_F(ArcCollectorTest, ParseCrashLog) {
  std::stringstream stream;
  ArcCollector::CrashLogHeaderMap map;
  std::string exception_info;

  // Crash log should not be empty.
  EXPECT_FALSE(ArcCollector::ParseCrashLog(
      "system_app_crash", &stream, &map, &exception_info));

  // Header key should be followed by a colon.
  stream.clear();
  stream.str("Key");
  EXPECT_FALSE(ArcCollector::ParseCrashLog(
      "system_app_crash", &stream, &map, &exception_info));

  EXPECT_TRUE(FindLog("Header has unexpected format"));
  ClearLog();

  // Header value should not be empty.
  stream.clear();
  stream.str("Key:   ");
  EXPECT_FALSE(ArcCollector::ParseCrashLog(
      "system_app_crash", &stream, &map, &exception_info));

  EXPECT_TRUE(FindLog("Header has unexpected format"));
  ClearLog();

  // Parse a crash log with exception info.
  stream.clear();
  stream.str(kCrashLog + 1);  // Skip EOL.
  EXPECT_TRUE(ArcCollector::ParseCrashLog(
      "system_app_crash", &stream, &map, &exception_info));

  EXPECT_TRUE(GetLog().empty());

  EXPECT_EQ("com.arc.app", ArcCollector::GetCrashLogHeader(map, "Process"));
  EXPECT_EQ("fingerprint", ArcCollector::GetCrashLogHeader(map, "Build"));
  EXPECT_EQ("unknown", ArcCollector::GetCrashLogHeader(map, "Activity"));
  EXPECT_EQ("Line 1\nLine 2\nLine 3\n", exception_info);

  // Parse a crash log without exception info.
  stream.clear();
  stream.seekg(0);
  map.clear();
  exception_info.clear();
  EXPECT_TRUE(ArcCollector::ParseCrashLog(
      "system_app_anr", &stream, &map, &exception_info));

  EXPECT_TRUE(GetLog().empty());

  EXPECT_EQ("0xcafebabe", ArcCollector::GetCrashLogHeader(map, "Flags"));
  EXPECT_EQ("com.arc.app v1 (1.0)",
      ArcCollector::GetCrashLogHeader(map, "Package"));
  EXPECT_TRUE(exception_info.empty());
}

TEST_F(ArcContextTest, GetArcPid) {
  EXPECT_FALSE(ArcCollector::IsArcRunning());

  pid_t pid;
  EXPECT_FALSE(collector_->context().GetArcPid(&pid));
}

TEST_F(ArcContextTest, GetPidNamespace) {
  std::string ns;
  EXPECT_TRUE(collector_->context().GetPidNamespace(pid_, &ns));
  EXPECT_THAT(ns, testing::MatchesRegex("^pid:\\[[0-9]+\\]$"));
}

TEST_F(ArcContextTest, GetExeBaseName) {
  std::string exe;
  EXPECT_TRUE(collector_->context().GetExeBaseName(pid_, &exe));
  EXPECT_EQ("crash_reporter_test", exe);
}

// TODO(crbug.com/590044)
TEST_F(ArcContextTest, DISABLED_GetCommand) {
  std::string command;
  EXPECT_TRUE(collector_->context().GetCommand(pid_, &command));

  // TODO(domlaskowski): QEMU mishandles emulation of /proc/self/cmdline,
  // prepending QEMU flags to the command line of the emulated program.
  // Keep in sync with qargv[1] in qemu-binfmt-wrapper.c for now.
  EXPECT_EQ("-0", command);
}
