// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/filesystem_verifier_action.h"

#include <fcntl.h>

#include <set>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/message_loops/glib_message_loop.h>
#include <chromeos/message_loops/message_loop_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "update_engine/fake_system_state.h"
#include "update_engine/mock_hardware.h"
#include "update_engine/omaha_hash_calculator.h"
#include "update_engine/test_utils.h"
#include "update_engine/utils.h"

using chromeos::MessageLoop;
using std::set;
using std::string;
using std::vector;

namespace chromeos_update_engine {

class FilesystemVerifierActionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loop_.SetAsCurrent();
  }

  void TearDown() override {
    EXPECT_EQ(0, chromeos::MessageLoopRunMaxIterations(&loop_, 1));
  }

  // Returns true iff test has completed successfully.
  bool DoTest(bool terminate_early,
              bool hash_fail,
              PartitionType partition_type);

  chromeos::GlibMessageLoop loop_;
  FakeSystemState fake_system_state_;
};

class FilesystemVerifierActionTestDelegate : public ActionProcessorDelegate {
 public:
  explicit FilesystemVerifierActionTestDelegate(
      FilesystemVerifierAction* action)
      : action_(action), ran_(false), code_(ErrorCode::kError) {}
  void ExitMainLoop() {
    // We need to wait for the Action to call Cleanup.
    if (action_->IsCleanupPending()) {
      LOG(INFO) << "Waiting for Cleanup() to be called.";
      MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&FilesystemVerifierActionTestDelegate::ExitMainLoop,
                     base::Unretained(this)),
          base::TimeDelta::FromMilliseconds(100));
    } else {
      MessageLoop::current()->BreakLoop();
    }
  }
  void ProcessingDone(const ActionProcessor* processor, ErrorCode code) {
    ExitMainLoop();
  }
  void ProcessingStopped(const ActionProcessor* processor) {
    ExitMainLoop();
  }
  void ActionCompleted(ActionProcessor* processor,
                       AbstractAction* action,
                       ErrorCode code) {
    if (action->Type() == FilesystemVerifierAction::StaticType()) {
      ran_ = true;
      code_ = code;
    }
  }
  bool ran() const { return ran_; }
  ErrorCode code() const { return code_; }

 private:
  FilesystemVerifierAction* action_;
  bool ran_;
  ErrorCode code_;
};

void StartProcessorInRunLoop(ActionProcessor* processor,
                             FilesystemVerifierAction* filesystem_copier_action,
                             bool terminate_early) {
  processor->StartProcessing();
  if (terminate_early) {
    EXPECT_NE(nullptr, filesystem_copier_action);
    processor->StopProcessing();
  }
}

// TODO(garnold) Temporarily disabling this test, see chromium-os:31082 for
// details; still trying to track down the root cause for these rare write
// failures and whether or not they are due to the test setup or an inherent
// issue with the chroot environment, library versions we use, etc.
TEST_F(FilesystemVerifierActionTest, DISABLED_RunAsRootSimpleTest) {
  ASSERT_EQ(0, getuid());
  bool test = DoTest(false, false, PartitionType::kKernel);
  EXPECT_TRUE(test);
  if (!test)
    return;
  test = DoTest(false, false, PartitionType::kRootfs);
  EXPECT_TRUE(test);
}

bool FilesystemVerifierActionTest::DoTest(bool terminate_early,
                                          bool hash_fail,
                                          PartitionType partition_type) {
  // We need MockHardware to verify MarkUnbootable calls, but don't want
  // warnings about other usages.
  testing::NiceMock<MockHardware> mock_hardware;
  fake_system_state_.set_hardware(&mock_hardware);

  string a_loop_file;

  if (!(utils::MakeTempFile("a_loop_file.XXXXXX", &a_loop_file, nullptr))) {
    ADD_FAILURE();
    return false;
  }
  ScopedPathUnlinker a_loop_file_unlinker(a_loop_file);

  // Make random data for a.
  const size_t kLoopFileSize = 10 * 1024 * 1024 + 512;
  chromeos::Blob a_loop_data(kLoopFileSize);
  test_utils::FillWithData(&a_loop_data);


  // Write data to disk
  if (!(test_utils::WriteFileVector(a_loop_file, a_loop_data))) {
    ADD_FAILURE();
    return false;
  }

  // Attach loop devices to the files
  string a_dev;
  test_utils::ScopedLoopbackDeviceBinder a_dev_releaser(a_loop_file, &a_dev);
  if (!(a_dev_releaser.is_bound())) {
    ADD_FAILURE();
    return false;
  }

  LOG(INFO) << "verifying: "  << a_loop_file << " (" << a_dev << ")";

  bool success = true;

  // Set up the action objects
  InstallPlan install_plan;
  switch (partition_type) {
    case PartitionType::kRootfs:
      install_plan.rootfs_size = kLoopFileSize - (hash_fail ? 1 : 0);
      install_plan.install_path = a_dev;
      if (!OmahaHashCalculator::RawHashOfData(
          a_loop_data, &install_plan.rootfs_hash)) {
        ADD_FAILURE();
        success = false;
      }
      break;
    case PartitionType::kKernel:
      install_plan.kernel_size = kLoopFileSize - (hash_fail ? 1 : 0);
      install_plan.kernel_install_path = a_dev;
      if (!OmahaHashCalculator::RawHashOfData(
          a_loop_data, &install_plan.kernel_hash)) {
        ADD_FAILURE();
        success = false;
      }
      break;
    case PartitionType::kSourceRootfs:
      install_plan.source_path = a_dev;
      if (!OmahaHashCalculator::RawHashOfData(
          a_loop_data, &install_plan.source_rootfs_hash)) {
        ADD_FAILURE();
        success = false;
      }
      break;
    case PartitionType::kSourceKernel:
      install_plan.kernel_source_path = a_dev;
      if (!OmahaHashCalculator::RawHashOfData(
          a_loop_data, &install_plan.source_kernel_hash)) {
        ADD_FAILURE();
        success = false;
      }
      break;
  }

  EXPECT_CALL(mock_hardware,
              MarkKernelUnbootable(a_dev)).Times(
                  partition_type == PartitionType::kKernel ? 1 : 0);

  ActionProcessor processor;

  ObjectFeederAction<InstallPlan> feeder_action;
  FilesystemVerifierAction copier_action(&fake_system_state_, partition_type);
  ObjectCollectorAction<InstallPlan> collector_action;

  BondActions(&feeder_action, &copier_action);
  BondActions(&copier_action, &collector_action);

  FilesystemVerifierActionTestDelegate delegate(&copier_action);
  processor.set_delegate(&delegate);
  processor.EnqueueAction(&feeder_action);
  processor.EnqueueAction(&copier_action);
  processor.EnqueueAction(&collector_action);

  feeder_action.set_obj(install_plan);

  loop_.PostTask(FROM_HERE, base::Bind(&StartProcessorInRunLoop,
                                       &processor,
                                       &copier_action,
                                       terminate_early));
  loop_.Run();

  if (!terminate_early) {
    bool is_delegate_ran = delegate.ran();
    EXPECT_TRUE(is_delegate_ran);
    success = success && is_delegate_ran;
  } else {
    EXPECT_EQ(ErrorCode::kError, delegate.code());
    return (ErrorCode::kError == delegate.code());
  }
  if (hash_fail) {
    ErrorCode expected_exit_code =
        ((partition_type == PartitionType::kKernel ||
          partition_type == PartitionType::kSourceKernel) ?
         ErrorCode::kNewKernelVerificationError :
         ErrorCode::kNewRootfsVerificationError);
    EXPECT_EQ(expected_exit_code, delegate.code());
    return (expected_exit_code == delegate.code());
  }
  EXPECT_EQ(ErrorCode::kSuccess, delegate.code());

  // Make sure everything in the out_image is there
  chromeos::Blob a_out;
  if (!utils::ReadFile(a_dev, &a_out)) {
    ADD_FAILURE();
    return false;
  }
  const bool is_a_file_reading_eq =
      test_utils::ExpectVectorsEq(a_loop_data, a_out);
  EXPECT_TRUE(is_a_file_reading_eq);
  success = success && is_a_file_reading_eq;

  bool is_install_plan_eq = (collector_action.object() == install_plan);
  EXPECT_TRUE(is_install_plan_eq);
  success = success && is_install_plan_eq;

  LOG(INFO) << "Verifying bootable flag on: " << a_dev;
  bool bootable;
  EXPECT_TRUE(mock_hardware.fake().IsKernelBootable(a_dev, &bootable));
  // We should always mark a partition as unbootable if it's a kernel
  // partition, but never if it's anything else.
  EXPECT_EQ(bootable, (partition_type != PartitionType::kKernel));

  return success;
}

class FilesystemVerifierActionTest2Delegate : public ActionProcessorDelegate {
 public:
  void ActionCompleted(ActionProcessor* processor,
                       AbstractAction* action,
                       ErrorCode code) {
    if (action->Type() == FilesystemVerifierAction::StaticType()) {
      ran_ = true;
      code_ = code;
    }
  }
  bool ran_;
  ErrorCode code_;
};

TEST_F(FilesystemVerifierActionTest, MissingInputObjectTest) {
  ActionProcessor processor;
  FilesystemVerifierActionTest2Delegate delegate;

  processor.set_delegate(&delegate);

  FilesystemVerifierAction copier_action(&fake_system_state_,
                                         PartitionType::kRootfs);
  ObjectCollectorAction<InstallPlan> collector_action;

  BondActions(&copier_action, &collector_action);

  processor.EnqueueAction(&copier_action);
  processor.EnqueueAction(&collector_action);
  processor.StartProcessing();
  EXPECT_FALSE(processor.IsRunning());
  EXPECT_TRUE(delegate.ran_);
  EXPECT_EQ(ErrorCode::kError, delegate.code_);
}

TEST_F(FilesystemVerifierActionTest, NonExistentDriveTest) {
  ActionProcessor processor;
  FilesystemVerifierActionTest2Delegate delegate;

  processor.set_delegate(&delegate);

  ObjectFeederAction<InstallPlan> feeder_action;
  InstallPlan install_plan(false,
                           false,
                           "",
                           0,
                           "",
                           0,
                           "",
                           "/no/such/file",
                           "/no/such/file",
                           "/no/such/file",
                           "/no/such/file",
                           "");
  feeder_action.set_obj(install_plan);
  FilesystemVerifierAction verifier_action(&fake_system_state_,
                                           PartitionType::kRootfs);
  ObjectCollectorAction<InstallPlan> collector_action;

  BondActions(&verifier_action, &collector_action);

  processor.EnqueueAction(&feeder_action);
  processor.EnqueueAction(&verifier_action);
  processor.EnqueueAction(&collector_action);
  processor.StartProcessing();
  EXPECT_FALSE(processor.IsRunning());
  EXPECT_TRUE(delegate.ran_);
  EXPECT_EQ(ErrorCode::kError, delegate.code_);
}

TEST_F(FilesystemVerifierActionTest, RunAsRootVerifyHashTest) {
  ASSERT_EQ(0, getuid());
  EXPECT_TRUE(DoTest(false, false, PartitionType::kRootfs));
  EXPECT_TRUE(DoTest(false, false, PartitionType::kKernel));
  EXPECT_TRUE(DoTest(false, false, PartitionType::kSourceRootfs));
  EXPECT_TRUE(DoTest(false, false, PartitionType::kSourceKernel));
}

TEST_F(FilesystemVerifierActionTest, RunAsRootVerifyHashFailTest) {
  ASSERT_EQ(0, getuid());
  EXPECT_TRUE(DoTest(false, true, PartitionType::kRootfs));
  EXPECT_TRUE(DoTest(false, true, PartitionType::kKernel));
}

TEST_F(FilesystemVerifierActionTest, RunAsRootTerminateEarlyTest) {
  ASSERT_EQ(0, getuid());
  EXPECT_TRUE(DoTest(true, false, PartitionType::kKernel));
}

TEST_F(FilesystemVerifierActionTest, RunAsRootDetermineFilesystemSizeTest) {
  string img;
  EXPECT_TRUE(utils::MakeTempFile("img.XXXXXX", &img, nullptr));
  ScopedPathUnlinker img_unlinker(img);
  test_utils::CreateExtImageAtPath(img, nullptr);
  // Extend the "partition" holding the file system from 10MiB to 20MiB.
  EXPECT_EQ(0, truncate(img.c_str(), 20 * 1024 * 1024));

  for (int i = 0; i < 2; ++i) {
    PartitionType fs_type =
        i ? PartitionType::kSourceKernel : PartitionType::kSourceRootfs;
    FilesystemVerifierAction action(&fake_system_state_, fs_type);
    EXPECT_EQ(kint64max, action.remaining_size_);
    {
      int fd = HANDLE_EINTR(open(img.c_str(), O_RDONLY));
      EXPECT_GT(fd, 0);
      ScopedFdCloser fd_closer(&fd);
      action.DetermineFilesystemSize(fd);
    }
    EXPECT_EQ(i ? kint64max : 10 * 1024 * 1024,
              action.remaining_size_);
  }
}

}  // namespace chromeos_update_engine
