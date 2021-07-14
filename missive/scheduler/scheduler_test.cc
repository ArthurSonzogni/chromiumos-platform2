// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/check_op.h>
#include <base/run_loop.h>
#include <base/sequence_checker.h>
#include <base/memory/weak_ptr.h>
#include <base/test/task_environment.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/scheduler/scheduler.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::WithArgs;

namespace reporting {
namespace {

class TestCallbackWaiter {
 public:
  TestCallbackWaiter() : run_loop_(std::make_unique<base::RunLoop>()) {}

  virtual void Signal() { run_loop_->Quit(); }

  void Complete() { Signal(); }

  void Wait() { run_loop_->Run(); }

 protected:
  std::unique_ptr<base::RunLoop> run_loop_;
};

class FakeJob : public Scheduler::Job {
 public:
  using StartCallback = base::OnceCallback<void()>;
  using ReportCompletionCallback = base::OnceCallback<Status()>;
  using CancelCallback = base::OnceCallback<Status(Status)>;

  class FakeJobDelegate : public Scheduler::Job::JobDelegate {
   public:
    FakeJobDelegate(ReportCompletionCallback report_completion_callback,
                    CancelCallback cancel_callback)
        : report_completion_callback_(std::move(report_completion_callback)),
          cancel_callback_(std::move(cancel_callback)) {}

   private:
    Status Complete() override {
      return std::move(report_completion_callback_).Run();
    }

    Status Cancel(Status status) override {
      return std::move(cancel_callback_).Run(status);
    }

    ReportCompletionCallback report_completion_callback_;
    CancelCallback cancel_callback_;
  };

  explicit FakeJob(std::unique_ptr<FakeJobDelegate> fake_job_delegate)
      : Job(std::move(fake_job_delegate)) {
    DETACH_FROM_SEQUENCE(sequence_checker_);
  }

  ~FakeJob() override { DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_); }

  void SetFinishStatus(Status status) { finish_status_ = status; }

 protected:
  void StartImpl() override {
    DCHECK(base::SequencedTaskRunnerHandle::IsSet());
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&FakeJob::Finish, weak_ptr_factory_.GetWeakPtr(),
                       finish_status_));
  }

  Status finish_status_{Status::StatusOK()};
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<FakeJob> weak_ptr_factory_{this};
};

class JobTest : public ::testing::Test {
 public:
  JobTest() = default;

  void SetUp() override {
    report_completion_callback_ = base::BindRepeating(
        [](std::atomic<size_t>* completion_counter,
           TestCallbackWaiter* complete_waiter) {
          *completion_counter += 1u;
          complete_waiter->Signal();
          return Status::StatusOK();
        },
        &completion_counter_, &complete_waiter_);

    cancel_callback_ = base::BindRepeating(
        [](std::atomic<size_t>* cancel_counter,
           TestCallbackWaiter* complete_waiter, Status status) {
          EXPECT_TRUE(!status.ok());
          *cancel_counter += 1u;
          complete_waiter->Signal();
          return Status::StatusOK();
        },
        &cancel_counter_, &complete_waiter_);
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  std::atomic<size_t> completion_counter_{0};
  std::atomic<size_t> cancel_counter_{0};
  TestCallbackWaiter complete_waiter_;
  base::RepeatingCallback<Status()> report_completion_callback_;
  base::RepeatingCallback<Status(Status)> cancel_callback_;
};

TEST_F(JobTest, WillStartOnceWithOKStatusAndReportCompletion) {
  auto delegate = std::make_unique<FakeJob::FakeJobDelegate>(
      report_completion_callback_, cancel_callback_);
  FakeJob job(std::move(delegate));

  TestCallbackWaiter waiter;
  job.Start(base::BindOnce(
      [](TestCallbackWaiter* waiter, Status status) {
        EXPECT_TRUE(status.ok());
        waiter->Signal();
      },
      &waiter));
  complete_waiter_.Wait();
  waiter.Wait();

  // The job should have finished successfully.
  EXPECT_EQ(completion_counter_, 1u);
  EXPECT_EQ(cancel_counter_, 0u);
  EXPECT_EQ(job.GetJobState(), Scheduler::Job::JobState::COMPLETED);

  // Now that the job has completed successfully, it shouldn't be startable, or
  // cancellable.
  TestCallbackWaiter waiter2;
  job.Start(base::BindOnce(
      [](TestCallbackWaiter* waiter, Status status) {
        EXPECT_TRUE(!status.ok());
        waiter->Signal();
      },
      &waiter2));
  waiter2.Wait();

  // Nothing should have changed from before.
  EXPECT_EQ(completion_counter_, 1u);
  EXPECT_EQ(cancel_counter_, 0u);
  EXPECT_EQ(job.GetJobState(), Scheduler::Job::JobState::COMPLETED);

  EXPECT_FALSE(job.Cancel(Status(error::INTERNAL, "Failing for tests")).ok());

  // Nothing should have changed from before.
  EXPECT_EQ(completion_counter_, 1u);
  EXPECT_EQ(cancel_counter_, 0u);
  EXPECT_EQ(job.GetJobState(), Scheduler::Job::JobState::COMPLETED);
}

TEST_F(JobTest, CancelsWhenJobFails) {
  FakeJob job(std::make_unique<FakeJob::FakeJobDelegate>(
      report_completion_callback_, cancel_callback_));
  job.SetFinishStatus(Status(error::INTERNAL, "Failing for tests"));

  TestCallbackWaiter waiter;
  job.Start(base::BindOnce(
      [](TestCallbackWaiter* waiter, Status status) {
        EXPECT_TRUE(status.ok());
        waiter->Signal();
      },
      &waiter));
  complete_waiter_.Wait();
  waiter.Wait();

  // The job should have finished successfully.
  EXPECT_EQ(completion_counter_, 0u);
  EXPECT_EQ(cancel_counter_, 1u);
  EXPECT_EQ(job.GetJobState(), Scheduler::Job::JobState::CANCELLED);
}

TEST_F(JobTest, WillNotStartWithNonOKStatusAndCancels) {
  FakeJob job(std::make_unique<FakeJob::FakeJobDelegate>(
      report_completion_callback_, cancel_callback_));

  EXPECT_TRUE(job.Cancel(Status(error::INTERNAL, "Failing For Tests")).ok());

  TestCallbackWaiter waiter;
  job.Start(base::BindOnce(
      [](TestCallbackWaiter* waiter, Status status) {
        EXPECT_TRUE(!status.ok());
        waiter->Signal();
      },
      &waiter));
  waiter.Wait();
}

class TestCallbackWaiterWithCounter : public TestCallbackWaiter {
 public:
  explicit TestCallbackWaiterWithCounter(size_t counter_limit)
      : counter_limit_(counter_limit) {
    DCHECK_GT(counter_limit_, 0u);
  }

  void Signal() override {
    size_t old_limit = counter_limit_.fetch_sub(1u);
    DCHECK_GT(old_limit, 0u);
    if (old_limit == 1u) {
      run_loop_->Quit();
    }
  }

 private:
  std::atomic<size_t> counter_limit_;
};

class TestSchedulerObserver : public Scheduler::SchedulerObserver {
 public:
  void Notify(Notification notification) override {
    switch (notification) {
      case (Notification::ACCEPTED_JOB):
        accepted_jobs_ += 1u;
        break;
      case (Notification::REJECTED_JOB):
        rejected_jobs_ += 1u;
        break;
      case (Notification::BLOCKED_JOB):
        blocked_jobs_ += 1u;
        break;
      case (Notification::STARTED_JOB):
        started_jobs_ += 1u;
        break;
      case (Notification::SUCCESSFUL_COMPLETION):
        successful_jobs_ += 1u;
        break;
      case (Notification::UNSUCCESSFUL_COMPLETION):
        unsuccessful_jobs_ += 1u;
        break;
      case (Notification::MEMORY_PRESSURE_CANCELLATION):
        memory_pressure_cancelled_jobs_ += 1u;
        break;
    }
  }

  std::atomic<size_t> accepted_jobs_{0u};
  std::atomic<size_t> rejected_jobs_{0u};
  std::atomic<size_t> blocked_jobs_{0u};
  std::atomic<size_t> started_jobs_{0u};
  std::atomic<size_t> successful_jobs_{0u};
  std::atomic<size_t> unsuccessful_jobs_{0u};
  std::atomic<size_t> memory_pressure_cancelled_jobs_{0u};
};

class SchedulerTest : public ::testing::Test {
 public:
  SchedulerTest() = default;

  void SetUp() override { scheduler_.AddObserver(&scheduler_observer_); }

  void TearDown() override {
    // Let everything ongoing to finish.
    task_environment_.RunUntilIdle();
  }

 protected:
  base::test::TaskEnvironment task_environment_{};
  Scheduler scheduler_;
  TestSchedulerObserver scheduler_observer_;
};

TEST_F(SchedulerTest, SchedulesAndRunsJobs) {
  // Many tests rely on "half" of jobs failing. For this reason kNumJobs should
  // be even.
  const size_t kNumJobs = 10u;

  TestCallbackWaiterWithCounter complete_waiter{kNumJobs};

  std::atomic<size_t> completion_counter = 0;
  base::RepeatingCallback<Status()> report_completion_callback =
      base::BindRepeating(
          [](std::atomic<size_t>* counter,
             TestCallbackWaiterWithCounter* waiter) {
            *counter += 1;
            waiter->Signal();
            return Status::StatusOK();
          },
          &completion_counter, &complete_waiter);

  std::atomic<size_t> cancel_counter = 0;
  base::RepeatingCallback<Status(Status)> cancel_callback = base::BindRepeating(
      [](std::atomic<size_t>* counter, TestCallbackWaiterWithCounter* waiter,
         Status status) {
        *counter += 1;
        waiter->Signal();
        return Status(error::INTERNAL, "Failing for tests");
      },
      &cancel_counter, &complete_waiter);

  for (size_t i = 0; i < kNumJobs; i++) {
    std::unique_ptr<FakeJob> job =
        std::make_unique<FakeJob>(std::make_unique<FakeJob::FakeJobDelegate>(
            report_completion_callback, cancel_callback));
    if (i % 2u == 0) {
      job->SetFinishStatus(Status(error::INTERNAL, "Failing for tests"));
    }
    scheduler_.EnqueueJob(std::move(job));
  }
  complete_waiter.Wait();
  task_environment_.RunUntilIdle();

  ASSERT_EQ(scheduler_observer_.accepted_jobs_, kNumJobs);

  // We should have at least kNumJobs * 2 blocks.
  EXPECT_GE(scheduler_observer_.blocked_jobs_, kNumJobs * 2);

  // We should have at least kNumJobs started.
  EXPECT_EQ(scheduler_observer_.started_jobs_, kNumJobs);

  // Half the jobs should complete successfully.
  EXPECT_EQ(scheduler_observer_.successful_jobs_, kNumJobs / 2u);

  // Half the jobs should complete unsuccessfully.
  EXPECT_EQ(scheduler_observer_.unsuccessful_jobs_, kNumJobs / 2u);

  // TODO(1174889) Once memory pressure is enabled, update tests to cause memory
  // pressure issues and ensure jobs are cancelled. At that time we can also
  // test rejected jobs.
  EXPECT_EQ(scheduler_observer_.rejected_jobs_, 0u);

  // Half the jobs should have been cancelled, while the other half should have
  // completed successfully.
  EXPECT_EQ(completion_counter, kNumJobs / 2u);
  EXPECT_EQ(cancel_counter, kNumJobs / 2u);
}

// TODO(b/193577465): Add test for Scheduler been destructed before all jobs
// have been run. This might require changes in Scheduler itself.

}  // namespace
}  // namespace reporting
