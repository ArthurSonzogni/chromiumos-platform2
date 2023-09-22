// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>

#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/functional/callback_helpers.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <spaced/proto_bindings/spaced.pb.h>
// NOLINTNEXTLINE(build/include_alpha) dbus-proxy-mocks.h needs spaced.pb.h
#include <spaced/dbus-proxy-mocks.h>

#include "diagnostics/base/mojo_utils.h"
#include "diagnostics/cros_healthd/executor/utils/fake_process_control.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/routine_adapter.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_service.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/routines/storage/disk_read.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ash::cros_healthd::mojom;
using ::testing::_;
using ::testing::InSequence;
using ::testing::WithArg;

class DiskReadRoutineTest : public testing::Test {
 protected:
  DiskReadRoutineTest() = default;
  DiskReadRoutineTest(const DiskReadRoutineTest&) = delete;
  DiskReadRoutineTest& operator=(const DiskReadRoutineTest&) = delete;

  void SetUp() override {
    // Set sufficient free space.
    SetGetFioTestDirectoryFreeSpaceResponse(
        /*free_space_byte=*/static_cast<int64_t>(10240 /*MiB*/) * 1024 * 1024);

    auto routine = DiskReadRoutine::Create(
        &mock_context_,
        mojom::DiskReadRoutineArgument::New(
            mojom::DiskReadTypeEnum::kLinearRead,
            /*disk_read_duration=*/base::Seconds(5), /*file_size_mib=*/64));
    CHECK(routine.has_value());
    routine_ = std::move(routine.value());
  }

  void SetRunFioPrepareResponse() {
    EXPECT_CALL(*mock_executor(), RunFio(_, _))
        .WillOnce(WithArg<1>(
            [=](mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver) {
              fake_process_control_prepare_.BindReceiver(std::move(receiver));
            }));
  }

  void SetRunFioReadResponse() {
    EXPECT_CALL(*mock_executor(), RunFio(_, _))
        .WillOnce(WithArg<1>(
            [=](mojo::PendingReceiver<ash::cros_healthd::mojom::ProcessControl>
                    receiver) {
              fake_process_control_read_.BindReceiver(std::move(receiver));
            }));
  }

  void SetRemoveFioTestFileResponse(int return_code = EXIT_SUCCESS) {
    EXPECT_CALL(*mock_executor(), RemoveFioTestFile(_))
        .WillOnce(WithArg<0>(
            [=](mojom::Executor::RemoveFioTestFileCallback callback) {
              mojom::ExecutedProcessResult result;
              result.return_code = return_code;
              std::move(callback).Run(result.Clone());
            }));
  }

  void SetGetFioTestDirectoryFreeSpaceResponse(
      std::optional<int64_t> free_space_byte) {
    if (free_space_byte.has_value()) {
      ON_CALL(*mock_spaced_proxy(), GetFreeDiskSpaceAsync(_, _, _, _))
          .WillByDefault(
              WithArg<1>([=](base::OnceCallback<void(int64_t /*reply*/)>
                                 success_callback) {
                std::move(success_callback).Run(free_space_byte.value());
              }));
    } else {
      ON_CALL(*mock_spaced_proxy(), GetFreeDiskSpaceAsync(_, _, _, _))
          .WillByDefault(WithArg<2>(
              [](base::OnceCallback<void(brillo::Error*)> error_callback) {
                auto error = brillo::Error::Create(FROM_HERE, "", "", "");
                std::move(error_callback).Run(error.get());
              }));
    }
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
    base::test::TestFuture<void> signal;
    RoutineObserverForTesting observer{signal.GetCallback()};
    routine_->SetObserver(observer.receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    EXPECT_TRUE(signal.Wait());
    return std::move(observer.state_);
  }

  void RunRoutineAndWaitForException(const std::string& expected_reason) {
    base::test::TestFuture<uint32_t, const std::string&> future;
    routine_->SetOnExceptionCallback(future.GetCallback());
    routine_->Start();
    EXPECT_EQ(future.Get<std::string>(), expected_reason)
        << "Unexpected reason in exception.";
  }

  void SetPrepareJobProcessResult(int return_code, const std::string& err) {
    fake_process_control_prepare_.SetReturnCode(return_code);
    fake_process_control_prepare_.SetStderrFileContent(err);
  }

  void SetReadJobProcessResult(int return_code, const std::string& err) {
    fake_process_control_read_.SetReturnCode(return_code);
    fake_process_control_read_.SetStderrFileContent(err);
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }
  org::chromium::SpacedProxyMock* mock_spaced_proxy() {
    return mock_context_.mock_spaced_proxy();
  }

  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<DiskReadRoutine> routine_;
  FakeProcessControl fake_process_control_prepare_;
  FakeProcessControl fake_process_control_read_;
};

class DiskReadRoutineAdapterTest : public DiskReadRoutineTest {
 protected:
  DiskReadRoutineAdapterTest() = default;
  DiskReadRoutineAdapterTest(const DiskReadRoutineAdapterTest&) = delete;
  DiskReadRoutineAdapterTest& operator=(const DiskReadRoutineAdapterTest&) =
      delete;

  void SetUp() override {
    // Set sufficient free space.
    SetGetFioTestDirectoryFreeSpaceResponse(
        /*free_space_byte=*/static_cast<uint64_t>(10240 /*MiB*/) * 1024 * 1024);

    SetUpRoutine(mojom::DiskReadTypeEnum::kLinearRead,
                 /*disk_read_duration=*/base::Seconds(5), /*file_size_mib=*/64);
  }

  // Utility function to flush the routine control and process control prepare.
  void FlushAdapterForPrepareJob() {
    CHECK(fake_process_control_prepare_.IsConnected());

    // Flush the process control to get return code.
    fake_process_control_prepare_.receiver().FlushForTesting();
    // Flush the process control to get stderr.
    fake_process_control_prepare_.receiver().FlushForTesting();

    // Flush the routine control to run any callbacks called by
    // fake_process_control_prepare_.
    routine_adapter_->FlushRoutineControlForTesting();
  }

  // Utility function to flush the routine control and process control read.
  void FlushAdapterForReadJob() {
    CHECK(fake_process_control_read_.IsConnected());

    // Flush the process control to get return code.
    fake_process_control_read_.receiver().FlushForTesting();
    // Flush the process control to get stderr.
    fake_process_control_read_.receiver().FlushForTesting();

    // Flush the routine control to run any callbacks called by
    // fake_process_control_read_.
    routine_adapter_->FlushRoutineControlForTesting();
  }

  void CheckRoutineUpdate(uint32_t progress_percent,
                          mojom::DiagnosticRoutineStatusEnum status,
                          const std::string& status_message = "") {
    routine_adapter_->PopulateStatusUpdate(&update_, true);
    EXPECT_EQ(update_.progress_percent, progress_percent);
    VerifyNonInteractiveUpdate(update_.routine_update_union, status,
                               status_message);
  }

  void SetUpRoutine(mojom::DiskReadTypeEnum read_type,
                    base::TimeDelta disk_read_duration,
                    uint32_t file_size_mib) {
    routine_adapter_ = std::make_unique<RoutineAdapter>(
        mojom::RoutineArgument::Tag::kDiskRead);
    routine_adapter_->SetupAdapter(
        mojom::RoutineArgument::NewDiskRead(mojom::DiskReadRoutineArgument::New(
            read_type, disk_read_duration, file_size_mib)),
        &routine_service_);
  }

  RoutineService routine_service_{&mock_context_};
  std::unique_ptr<RoutineAdapter> routine_adapter_;
  mojom::RoutineUpdate update_{0, mojo::ScopedHandle(),
                               mojom::RoutineUpdateUnionPtr()};
};

// Test that the disk read routine can run successfully.
TEST_F(DiskReadRoutineTest, RoutineSuccess) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  SetRunFioPrepareResponse();
  SetRunFioReadResponse();
  SetPrepareJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetReadJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  // Function call in destructor.
  SetRemoveFioTestFileResponse();

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  EXPECT_TRUE(result->state_union->is_finished());
  EXPECT_TRUE(result->state_union->get_finished()->has_passed);
}

// Test that the disk read routine can run successfully with routine adapter.
TEST_F(DiskReadRoutineAdapterTest, RoutineSuccess) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  SetRunFioPrepareResponse();
  SetRunFioReadResponse();
  SetPrepareJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetReadJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  // Called in deconstructor to handle unexpected routine failures.
  SetRemoveFioTestFileResponse();

  routine_adapter_->Start();
  // Flush the routine for all request to executor through process control.
  routine_adapter_->FlushRoutineControlForTesting();

  FlushAdapterForPrepareJob();
  CheckRoutineUpdate(50, mojom::DiagnosticRoutineStatusEnum::kRunning);

  FlushAdapterForReadJob();
  CheckRoutineUpdate(100, mojom::DiagnosticRoutineStatusEnum::kPassed);
}

// Test that the disk read routine handles the error of retrieving free space.
TEST_F(DiskReadRoutineTest, RoutineRetrieveFreeSpaceError) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);
  SetGetFioTestDirectoryFreeSpaceResponse(/*free_space_byte=*/std::nullopt);

  // Function call in destructor.
  SetRemoveFioTestFileResponse();

  RunRoutineAndWaitForException("Failed to retrieve free storage space");
}

// Test that the disk read routine handles the error of retrieving free space
// with routine adapter.
TEST_F(DiskReadRoutineAdapterTest, RoutineRetrieveFreeSpaceError) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);
  SetGetFioTestDirectoryFreeSpaceResponse(/*free_space_byte=*/std::nullopt);

  // Called in deconstructor to handle unexpected routine failures.
  SetRemoveFioTestFileResponse();

  routine_adapter_->Start();
  // Flush the routine for all request to executor through process control.
  routine_adapter_->FlushRoutineControlForTesting();

  CheckRoutineUpdate(0, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Failed to retrieve free storage space");
}

// Test that the disk read routine handles insufficient free space error.
TEST_F(DiskReadRoutineTest, RoutineInsufficientFreeSpaceError) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);
  SetGetFioTestDirectoryFreeSpaceResponse(/*free_space_byte=*/0);

  // Function call in destructor.
  SetRemoveFioTestFileResponse();

  RunRoutineAndWaitForException("Failed to reserve sufficient storage space");
}

// Test that the disk read routine handles insufficient free space error with
// routine adapter.
TEST_F(DiskReadRoutineAdapterTest, RoutineInsufficientFreeSpaceError) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);
  SetGetFioTestDirectoryFreeSpaceResponse(/*free_space_byte=*/0);

  // Called in deconstructor to handle unexpected routine failures.
  SetRemoveFioTestFileResponse();

  routine_adapter_->Start();
  // Flush the routine for all request to executor through process control.
  routine_adapter_->FlushRoutineControlForTesting();

  CheckRoutineUpdate(0, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Failed to reserve sufficient storage space");
}

// Test that the disk read routine handles the error of running fio prepare.
TEST_F(DiskReadRoutineTest, RoutineRunFioPrepareError) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  SetRunFioPrepareResponse();
  SetPrepareJobProcessResult(EXIT_FAILURE, /*err=*/"prepare job error");

  // Function call in destructor.
  SetRemoveFioTestFileResponse();

  RunRoutineAndWaitForException("Failed to complete fio prepare job");
}

// Test that the disk read routine handles the error of running fio prepare with
// routine adapter.
TEST_F(DiskReadRoutineAdapterTest, RoutineRunFioPrepareError) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  SetRunFioPrepareResponse();
  SetPrepareJobProcessResult(EXIT_FAILURE, /*err=*/"prepare job error");

  // Called in deconstructor to handle unexpected routine failures.
  SetRemoveFioTestFileResponse();

  routine_adapter_->Start();
  // Flush the routine for all request to executor through process control.
  routine_adapter_->FlushRoutineControlForTesting();

  FlushAdapterForPrepareJob();
  CheckRoutineUpdate(0, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Failed to complete fio prepare job");
}

// Test that the disk read routine handles the error of running fio read.
TEST_F(DiskReadRoutineTest, RoutineRunFioReadError) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  SetRunFioPrepareResponse();
  SetRunFioReadResponse();
  SetPrepareJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetReadJobProcessResult(EXIT_FAILURE, /*err=*/"read job error");

  // Function call in destructor.
  SetRemoveFioTestFileResponse();

  RunRoutineAndWaitForException("Failed to complete fio read job");
}

// Test that the disk read routine handles the error of running fio read with
// routine adapter.
TEST_F(DiskReadRoutineAdapterTest, RoutineRunFioReadError) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  SetRunFioPrepareResponse();
  SetRunFioReadResponse();
  SetPrepareJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetReadJobProcessResult(EXIT_FAILURE, /*err=*/"read job error");

  // Called in deconstructor to handle unexpected routine failures.
  SetRemoveFioTestFileResponse();

  routine_adapter_->Start();
  // Flush the routine for all request to executor through process control.
  routine_adapter_->FlushRoutineControlForTesting();

  FlushAdapterForPrepareJob();
  FlushAdapterForReadJob();
  CheckRoutineUpdate(0, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Failed to complete fio read job");
}

// Test that the disk read routine handles the error of cleaning fio test file
// when routine starts.
TEST_F(DiskReadRoutineTest, RoutineRemoveFioTestFileErrorOnStart) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_FAILURE);

  // Function call in destructor.
  SetRemoveFioTestFileResponse();

  RunRoutineAndWaitForException("Failed to clean up storage");
}

// Test that the disk read routine handles the error of cleaning fio test file
// with routine adapter when routine starts.
TEST_F(DiskReadRoutineAdapterTest, RoutineCleanFioTestFileErrorOnStart) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_FAILURE);

  // Called in deconstructor to handle unexpected routine failures.
  SetRemoveFioTestFileResponse();

  routine_adapter_->Start();
  // Flush the routine for all request to executor through process control.
  routine_adapter_->FlushRoutineControlForTesting();
  CheckRoutineUpdate(0, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Failed to clean up storage");
}

// Test that the disk read routine handles the error of cleaning fio test file
// when routine completes.
TEST_F(DiskReadRoutineTest, RoutineRemoveFioTestFileErrorOnComplete) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  SetRunFioPrepareResponse();
  SetRunFioReadResponse();
  SetPrepareJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetReadJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_FAILURE);

  // Function call in destructor.
  SetRemoveFioTestFileResponse();

  RunRoutineAndWaitForException("Failed to clean up storage");
}

// Test that the disk read routine handles the error of cleaning fio test file
// with routine adapter when routine completes.
TEST_F(DiskReadRoutineAdapterTest, RoutineCleanFioTestFileErrorOnComplete) {
  InSequence s;
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_SUCCESS);

  SetRunFioPrepareResponse();
  SetRunFioReadResponse();
  SetPrepareJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetReadJobProcessResult(EXIT_SUCCESS, /*err=*/"");
  SetRemoveFioTestFileResponse(/*return_code=*/EXIT_FAILURE);

  // Called in deconstructor to handle unexpected routine failures.
  SetRemoveFioTestFileResponse();

  routine_adapter_->Start();
  // Flush the routine for all request to executor through process control.
  routine_adapter_->FlushRoutineControlForTesting();

  FlushAdapterForPrepareJob();
  FlushAdapterForReadJob();
  CheckRoutineUpdate(0, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Failed to clean up storage");
}

// Test that the disk read routine can not be created with zero disk read
// duration.
TEST_F(DiskReadRoutineTest, RoutineCreateErrorZeroDiskReadDuration) {
  auto routine = DiskReadRoutine::Create(
      &mock_context_,
      mojom::DiskReadRoutineArgument::New(
          mojom::DiskReadTypeEnum::kLinearRead,
          /*disk_read_duration=*/base::Seconds(0), /*file_size_mib=*/64));
  EXPECT_FALSE(routine.has_value());
}

// Test that the disk read routine can not be created with zero disk read
// duration with routine adapter.
TEST_F(DiskReadRoutineAdapterTest, RoutineCreateErrorZeroDiskReadDuration) {
  SetUpRoutine(mojom::DiskReadTypeEnum::kLinearRead,
               /*disk_read_duration=*/base::Seconds(0), /*file_size_mib=*/64);

  routine_adapter_->Start();
  // Flush the routine for all request through process control.
  routine_adapter_->FlushRoutineControlForTesting();
  CheckRoutineUpdate(0, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Disk read duration should not be zero after rounding "
                     "towards zero to the nearest second");
}

// Test that the disk read routine can not be created with zero test file size.
TEST_F(DiskReadRoutineTest, RoutineCreateErrorZeroTestFileSize) {
  auto routine = DiskReadRoutine::Create(
      &mock_context_,
      mojom::DiskReadRoutineArgument::New(
          mojom::DiskReadTypeEnum::kLinearRead,
          /*disk_read_duration=*/base::Seconds(5), /*file_size_mib=*/0));
  EXPECT_FALSE(routine.has_value());
}

// Test that the disk read routine can not be created with zero test file size
// with routine adapter.
TEST_F(DiskReadRoutineAdapterTest, RoutineCreateErrorZeroTestFileSize) {
  SetUpRoutine(mojom::DiskReadTypeEnum::kLinearRead,
               /*disk_read_duration=*/base::Seconds(5), /*file_size_mib=*/0);

  routine_adapter_->Start();
  // Flush the routine for all request through process control.
  routine_adapter_->FlushRoutineControlForTesting();
  CheckRoutineUpdate(0, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Test file size should not be zero");
}

// Test that the disk read routine can not be created with unexpected disk read
// type.
TEST_F(DiskReadRoutineTest, RoutineCreateErrorUnexpectedDiskReadType) {
  auto routine = DiskReadRoutine::Create(
      &mock_context_,
      mojom::DiskReadRoutineArgument::New(
          mojom::DiskReadTypeEnum::kUnmappedEnumField,
          /*disk_read_duration=*/base::Seconds(5), /*file_size_mib=*/64));
  EXPECT_FALSE(routine.has_value());
}

// Test that the disk read routine can not be created with unexpected disk read
// type with routine adapter.
TEST_F(DiskReadRoutineAdapterTest, RoutineCreateErrorUnexpectedDiskReadType) {
  SetUpRoutine(mojom::DiskReadTypeEnum::kUnmappedEnumField,
               /*disk_read_duration=*/base::Seconds(5), /*file_size_mib=*/64);

  routine_adapter_->Start();
  // Flush the routine for all request through process control.
  routine_adapter_->FlushRoutineControlForTesting();
  CheckRoutineUpdate(0, mojom::DiagnosticRoutineStatusEnum::kError,
                     "Unexpected disk read type");
}

}  // namespace
}  // namespace diagnostics
