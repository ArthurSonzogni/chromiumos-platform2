// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>

#include <string>

#include <base/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/process_mock.h>
#include <chromeos/test_helpers.h>
#include <gtest/gtest.h>

#include "vpn-manager/daemon.h"

using ::base::FilePath;
using ::base::IntToString;
using ::chromeos::Process;
using ::chromeos::ProcessImpl;
using ::chromeos::ProcessMock;
using ::std::string;
using ::testing::_;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::Return;

namespace vpn_manager {

class DaemonTest : public ::testing::Test {
 public:
  void SetUp() {
    FilePath cwd;
    CHECK(temp_dir_.CreateUniqueTempDir());
    FilePath test_path = temp_dir_.path().Append("daemon_testdir");
    base::DeleteFile(test_path, true);
    base::CreateDirectory(test_path);
    pid_file_path_ = test_path.Append("process.pid");
    daemon_.reset(new Daemon(pid_file_path_.value()));
  }

  // Needs to be public so we can use the testing::Invoke() family of functions.
  bool KillRealProcess() {
    return real_process_->Kill(SIGTERM, 5);
  }

 protected:
  void WritePidFile(const string& pid) {
    if (base::WriteFile(pid_file_path_, pid.c_str(), pid.size()) < 0) {
      LOG(ERROR) << "Unable to create " << pid_file_path_.value();
    }
  }

  void MakeRealProcess() {
    real_process_.reset(new ProcessImpl);
    real_process_->AddArg("sleep");
    real_process_->AddArg("12345");
    CHECK(real_process_->Start());
  }

  const string& GetPidFile() {
    return daemon_->pid_file_;
  }

  Process* GetProcess() {
    return daemon_->process_.get();
  }

  void SetProcess(Process* process) {
    daemon_->SetProcess(process);
  }

  FilePath pid_file_path_;
  scoped_ptr<Daemon> daemon_;
  scoped_ptr<Process> real_process_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(DaemonTest, Construction) {
  EXPECT_EQ(NULL, GetProcess());
  EXPECT_EQ(pid_file_path_.value(), GetPidFile());
  EXPECT_FALSE(daemon_->IsRunning());
}

TEST_F(DaemonTest, FindProcess) {
  EXPECT_FALSE(daemon_->FindProcess());
  EXPECT_FALSE(daemon_->IsRunning());

  // Start a real process and note its pid, then kill it so we know we have
  // a non-running pid.
  MakeRealProcess();
  pid_t pid = real_process_->pid();
  KillRealProcess();

  WritePidFile(IntToString(pid));
  EXPECT_FALSE(daemon_->FindProcess());
  EXPECT_EQ(NULL, GetProcess());

  MakeRealProcess();
  pid = real_process_->pid();

  WritePidFile(IntToString(pid));
  EXPECT_TRUE(daemon_->FindProcess());
  EXPECT_TRUE(GetProcess());
  EXPECT_EQ(pid, GetProcess()->pid());
}

TEST_F(DaemonTest, IsRunningAndGetPid) {
  EXPECT_FALSE(daemon_->IsRunning());
  EXPECT_EQ(0, daemon_->GetPid());

  MakeRealProcess();
  pid_t pid = real_process_->pid();
  ASSERT_NE(0, pid);
  SetProcess(real_process_.release());
  EXPECT_TRUE(daemon_->IsRunning());
  EXPECT_EQ(pid, daemon_->GetPid());

  // Kill the process outside of the view of the process owned by the daemon.
  scoped_ptr<Process> killed_process(new ProcessImpl);
  killed_process->Reset(pid);
  killed_process->Kill(SIGTERM, 5);
  EXPECT_FALSE(daemon_->IsRunning());
  EXPECT_EQ(pid, daemon_->GetPid());

  SetProcess(NULL);
  EXPECT_EQ(0, daemon_->GetPid());
}

TEST_F(DaemonTest, SetProcessFromNull) {
  EXPECT_EQ(NULL, GetProcess());
  SetProcess(NULL);  // Should be a no-op.
  ProcessMock* process0 = new ProcessMock;
  SetProcess(process0);  // Passes ownership.
  EXPECT_EQ(process0, GetProcess());
  // Called during destructor.
  EXPECT_CALL(*process0, pid()).WillOnce(Return(0));
}

TEST_F(DaemonTest, SetProcessToNullFromNotRunning) {
  ProcessMock* process = new ProcessMock;
  EXPECT_CALL(*process, Release()).Times(0);
  EXPECT_CALL(*process, pid()).WillOnce(Return(0));
  SetProcess(process);  // Passes ownership.
  SetProcess(NULL);
  EXPECT_EQ(NULL, GetProcess());
}

TEST_F(DaemonTest, SetProcessToNullFromRunning) {
  MakeRealProcess();
  ProcessMock* process = new ProcessMock;
  EXPECT_CALL(*process, Release()).Times(0);
  EXPECT_CALL(*process, pid()).WillRepeatedly(Return(real_process_->pid()));
  EXPECT_CALL(*process, Kill(SIGKILL, _)).Times(1);
  SetProcess(process);  // Passes ownership.
  SetProcess(NULL);
  EXPECT_EQ(NULL, GetProcess());
}

TEST_F(DaemonTest, SetProcessToDifferentPid) {
  MakeRealProcess();
  ProcessMock* process0 = new ProcessMock;
  EXPECT_CALL(*process0, Release()).Times(0);
  EXPECT_CALL(*process0, pid()).WillRepeatedly(Return(real_process_->pid()));
  EXPECT_CALL(*process0, Kill(SIGKILL, _)).Times(1);
  ProcessMock* process1 = new ProcessMock;
  EXPECT_CALL(*process1, Release()).Times(0);
  EXPECT_CALL(*process1, pid()).WillOnce(Return(2));
  SetProcess(process0);  // Passes ownership.
  SetProcess(process1);  // Passes ownership.
  EXPECT_EQ(process1, GetProcess());
  // Verify expectations now so we don't trigger on calls during the destructor.
  Mock::VerifyAndClearExpectations(process1);
  EXPECT_CALL(*process1, pid()).WillOnce(Return(0));
}

TEST_F(DaemonTest, SetProcessToSamePid) {
  ProcessMock* process0 = new ProcessMock;
  EXPECT_CALL(*process0, Release()).Times(1);
  EXPECT_CALL(*process0, pid()).WillOnce(Return(1));
  ProcessMock* process1 = new ProcessMock;
  EXPECT_CALL(*process1, Release()).Times(0);
  EXPECT_CALL(*process1, pid()).WillOnce(Return(1));
  SetProcess(process0);  // Passes ownership.
  SetProcess(process1);  // Passes ownership.
  EXPECT_EQ(process1, GetProcess());
  // Reset expectations now so we don't trigger on calls during the destructor.
  Mock::VerifyAndClearExpectations(process1);
  EXPECT_CALL(*process1, pid()).WillOnce(Return(0));
}

TEST_F(DaemonTest, TerminateNoProcess) {
  WritePidFile("");
  ASSERT_TRUE(base::PathExists(pid_file_path_));
  EXPECT_TRUE(daemon_->Terminate());
  EXPECT_FALSE(base::PathExists(pid_file_path_));
}

TEST_F(DaemonTest, TerminateDeadProcess) {
  ProcessMock* process = new ProcessMock;
  EXPECT_CALL(*process, pid()).Times(2).WillRepeatedly(Return(0));
  EXPECT_CALL(*process, Kill(SIGTERM, _)).Times(0);
  SetProcess(process);  // Passes ownership.
  WritePidFile("");
  ASSERT_TRUE(base::PathExists(pid_file_path_));
  EXPECT_TRUE(daemon_->Terminate());
  EXPECT_FALSE(base::PathExists(pid_file_path_));
}

TEST_F(DaemonTest, TerminateLiveProcess) {
  MakeRealProcess();
  ProcessMock* process = new ProcessMock;
  EXPECT_CALL(*process, pid()).WillRepeatedly(Return(real_process_->pid()));
  EXPECT_CALL(*process, Kill(SIGTERM, _))
      .WillOnce(InvokeWithoutArgs(this, &DaemonTest::KillRealProcess));
  EXPECT_CALL(*process, Kill(SIGKILL, _)).Times(0);
  SetProcess(process);  // Passes ownership.
  WritePidFile("");
  ASSERT_TRUE(base::PathExists(pid_file_path_));
  // Returns false since the daemon was unable to terminate the process.
  EXPECT_TRUE(daemon_->Terminate());
  EXPECT_FALSE(base::PathExists(pid_file_path_));
}

TEST_F(DaemonTest, Destructor) {
  // This doesn't directly unit-test the Daemon class, but it does illuminate
  // a side effect of the destruction of the underlying Process it holds.
  MakeRealProcess();
  ProcessMock* process = new ProcessMock;
  EXPECT_CALL(*process, pid()).WillRepeatedly(Return(real_process_->pid()));
  EXPECT_CALL(*process, Kill(SIGKILL, _)).Times(1);
  SetProcess(process);  // Passes ownership.

  EXPECT_TRUE(daemon_->IsRunning());
  SetProcess(NULL);
  daemon_.reset();
}

}  // namespace vpn_manager

int main(int argc, char** argv) {
  SetUpTests(&argc, argv, true);
  return RUN_ALL_TESTS();
}
