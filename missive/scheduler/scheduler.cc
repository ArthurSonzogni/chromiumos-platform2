// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "missive/scheduler/scheduler.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/types/expected.h>

#include "missive/analytics/metrics.h"
#include "missive/util/reporting_errors.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"
#include "missive/util/task_runner_context.h"

namespace reporting {
namespace {

enum TaskLimit {
  NORMAL = 5,
  REDUCED = 2,
  OFF = 0,
};

}  // namespace

using CompleteJobResponse = Status;
using CompleteJobCallback = base::OnceCallback<void(CompleteJobResponse)>;
using Notification = Scheduler::SchedulerObserver::Notification;

Scheduler::Job::Job(
    std::unique_ptr<JobDelegate> job_response_delegate,
    scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner)
    : job_response_delegate_(std::move(job_response_delegate)),
      sequenced_task_runner_(sequenced_task_runner) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

Scheduler::Job::~Job() {
  CheckValidSequence();
}

void Scheduler::Job::CheckValidSequence() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

scoped_refptr<base::SequencedTaskRunner> Scheduler::Job::sequenced_task_runner()
    const {
  return sequenced_task_runner_;
}

void Scheduler::Job::Start(base::OnceCallback<void(Status)> complete_cb) {
  CHECK(complete_cb);
  // std::atomic<T>::compare_exchange_strong expects an lvalue for the expected
  // state.
  JobState expected_job_state = JobState::NOT_RUNNING;
  if (!job_state_.compare_exchange_strong(expected_job_state,
                                          /*desired=*/JobState::RUNNING)) {
    std::move(complete_cb)
        .Run(Status(
            error::UNAVAILABLE,
            "Job can only be started when it is in the NOT_RUNNING state."));

    analytics::Metrics::SendEnumToUMA(
        kUmaUnavailableErrorReason,
        UnavailableErrorReason::CANNOT_SCHEDULE_A_JOB_THATS_ALREADY_RUNNING,
        UnavailableErrorReason::MAX_VALUE);
    return;
  }

  complete_cb_ = std::move(complete_cb);
  StartImpl();
}

Status Scheduler::Job::DoCancel(Status status) {
  if (status.ok()) {
    return Status(error::INVALID_ARGUMENT,
                  "Job cannot be cancelled with an OK Status");
  }

  // std::atomic<T>::compare_exchange_strong expects an lvalue for the expected
  // state.
  JobState expected_job_state = JobState::NOT_RUNNING;
  if (!job_state_.compare_exchange_strong(expected_job_state,
                                          JobState::CANCELLED)) {
    analytics::Metrics::SendEnumToUMA(
        kUmaUnavailableErrorReason,
        UnavailableErrorReason::CANNOT_CANCEL_A_JOB_THATS_ALREADY_RUNNING,
        UnavailableErrorReason::MAX_VALUE);
    return Status(error::UNAVAILABLE,
                  "Job cannot be cancelled after it has started.");
  }

  return job_response_delegate_->Cancel(status);
}

void Scheduler::Job::Cancel(Status status) {
  auto cancel_status = DoCancel(status);
  LOG_IF(ERROR, !cancel_status.ok())
      << "Was unable to successfully cancel a job: " << cancel_status
      << ", status: " << status;
}

Scheduler::Job::JobState Scheduler::Job::GetJobState() const {
  return job_state_;
}

void Scheduler::Job::Finish(Status status) {
  CheckValidSequence();

  if (!status.ok()) {
    job_state_ = JobState::CANCELLED;
    std::move(complete_cb_).Run(job_response_delegate_->Cancel(status));
    return;
  }
  job_state_ = JobState::COMPLETED;
  std::move(complete_cb_).Run(job_response_delegate_->Complete());
}

class Scheduler::JobBlocker {
 public:
  JobBlocker(scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
             base::OnceClosure release_cb)
      : sequenced_task_runner_(sequenced_task_runner),
        release_cb_(std::move(release_cb)) {}

  ~JobBlocker() {
    sequenced_task_runner_->PostTask(FROM_HERE, std::move(release_cb_));
  }

  JobBlocker(const JobBlocker&) = delete;
  JobBlocker& operator=(const JobBlocker&) = delete;

 private:
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  base::OnceClosure release_cb_;
};

class Scheduler::JobSemaphore {
 public:
  JobSemaphore(scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
               TaskLimit task_limit)
      : sequenced_task_runner_(sequenced_task_runner), task_limit_(task_limit) {
    DETACH_FROM_SEQUENCE(sequence_checker_);
  }

  ~JobSemaphore() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (running_jobs_ > 0) {
      LOG(ERROR) << "JobSemaphore destructing with active jobs.";
    }
  }

  JobSemaphore(const JobSemaphore&) = delete;
  JobSemaphore& operator=(const JobSemaphore&) = delete;

  StatusOr<std::unique_ptr<JobBlocker>> AcquireJobBlocker() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (running_jobs_ >= task_limit_) {
      return base::unexpected(
          Status(error::RESOURCE_EXHAUSTED, "Currently at job limit"));
    }
    running_jobs_++;

    return std::make_unique<JobBlocker>(
        sequenced_task_runner_,
        base::BindOnce(&JobSemaphore::Release, weak_ptr_factory_.GetWeakPtr()));
  }

  void UpdateTaskLimit(TaskLimit task_limit) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    task_limit_ = task_limit;
  }

  // Returns true if the number of running jobs is within the limit.
  // Used when reasserting a job locker that is already acquired, before
  // assigning it to a new job.
  bool IsUnderTaskLimit() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return task_limit_ != TaskLimit::OFF && running_jobs_ <= task_limit_;
  }

  bool IsAcceptingJobs() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return task_limit_ != TaskLimit::OFF;
  }

 private:
  static void Release(base::WeakPtr<JobSemaphore> self) {
    if (!self) {
      return;
    }
    DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
    self->running_jobs_--;
  }

  SEQUENCE_CHECKER(sequence_checker_);

  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;

  TaskLimit task_limit_ GUARDED_BY_CONTEXT(sequence_checker_) = TaskLimit::OFF;
  uint32_t running_jobs_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;

  base::WeakPtrFactory<JobSemaphore> weak_ptr_factory_{this};
};

Scheduler::Scheduler()
    : sequenced_task_runner_(base::ThreadPool::CreateSequencedTaskRunner({})),
      job_semaphore_(
          new JobSemaphore(sequenced_task_runner_, TaskLimit::NORMAL),
          base::OnTaskRunnerDeleter(sequenced_task_runner_)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

Scheduler::~Scheduler() = default;

void Scheduler::AddObserver(SchedulerObserver* observer) {
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](SchedulerObserver* observer, base::WeakPtr<Scheduler> self) {
            if (!self) {
              return;
            }
            self->observers_.push_back(observer);
          },
          base::Unretained(observer), weak_ptr_factory_.GetWeakPtr()));
}

// static
void Scheduler::NotifyObservers(base::WeakPtr<Scheduler> self,
                                Notification notification) {
  if (!self) {
    return;
  }

  CHECK(base::SequencedTaskRunner::HasCurrentDefault());
  DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
  for (auto* observer : self->observers_) {
    observer->Notify(notification);
  }
}

void Scheduler::EnqueueJob(Job::SmartPtr<Job> job) {
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](Job::SmartPtr<Job> job, base::WeakPtr<Scheduler> self) {
            if (!self) {
              job->Cancel(Status(
                  error::UNAVAILABLE,
                  "Unable to enqueue job, Scheduler is no longer available"));
              return;
            }
            DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
            if (!self->job_semaphore_->IsAcceptingJobs()) {
              NotifyObservers(self->weak_ptr_factory_.GetWeakPtr(),
                              Notification::REJECTED_JOB);
              job->Cancel(Status(error::RESOURCE_EXHAUSTED,
                                 "Unable to process due to low system memory"));
              return;
            }
            self->jobs_queue_.push(std::move(job));
            NotifyObservers(self, Notification::ACCEPTED_JOB);
            StartJobs(self);
          },
          std::move(job), weak_ptr_factory_.GetWeakPtr()));
}

// static
void Scheduler::StartJobs(base::WeakPtr<Scheduler> self) {
  if (!self) {
    return;
  }

  CHECK(base::SequencedTaskRunner::HasCurrentDefault());
  DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);

  while (!self->jobs_queue_.empty()) {
    // Get JobBlockers and assign them to jobs until job_semaphore_ returns a
    // non-OK status.
    auto blocker_result = self->job_semaphore_->AcquireJobBlocker();
    if (!blocker_result.has_value()) {
      // Some jobs have been blocked.
      NotifyObservers(self, Notification::BLOCKED_JOB);
      return;
    }
    RunJob(self, std::move(blocker_result.value()),
           std::move(self->jobs_queue_.front()));
    self->jobs_queue_.pop();
  }
}

// static
void Scheduler::MaybeStartNextJob(base::WeakPtr<Scheduler> self,
                                  std::unique_ptr<JobBlocker> job_blocker) {
  if (!self) {
    return;
  }

  CHECK(base::SequencedTaskRunner::HasCurrentDefault());
  DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);

  if (self->jobs_queue_.empty()) {
    return;
  }
  if (self->job_semaphore_->IsUnderTaskLimit()) {
    RunJob(self, std::move(job_blocker), std::move(self->jobs_queue_.front()));
    self->jobs_queue_.pop();
    if (self->jobs_queue_.empty()) {
      return;  // Last job unblocked.
    }
  }
  // Some jobs remain blocked.
  self->NotifyObservers(self, Notification::BLOCKED_JOB);
}

// static
void Scheduler::RunJob(base::WeakPtr<Scheduler> self,
                       std::unique_ptr<JobBlocker> job_blocker,
                       Job::SmartPtr<Job> job) {
  if (!self) {
    job->Cancel(
        Status(error::UNAVAILABLE,
               "Unable to enqueue job, Scheduler is no longer available"));
    return;
  }

  CHECK(base::SequencedTaskRunner::HasCurrentDefault());
  DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
  CHECK(job_blocker);
  auto completion_cb = base::BindOnce(
      [](base::OnceClosure start_next_job_cb,
         base::OnceCallback<void(Notification)> notify_observers,
         Status job_result) {
        // The job has finished, pass its locker to the next one in the queue,
        // if any.
        std::move(start_next_job_cb).Run();
        if (!job_result.ok()) {
          std::move(notify_observers)
              .Run(Notification::UNSUCCESSFUL_COMPLETION);
          LOG(ERROR) << job_result;
          return;
        }
        std::move(notify_observers).Run(Notification::SUCCESSFUL_COMPLETION);
      },
      base::BindPostTask(
          self->sequenced_task_runner_,
          base::BindOnce(
              &Scheduler::MaybeStartNextJob, self,
              std::move(
                  job_blocker))),  // Hold at least until the job completes.
      base::BindOnce(&Scheduler::NotifyObservers, self));

  // Post task on an arbitrary thread, get back upon completion.
  raw_ptr<Job> job_ptr = job.get();
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::BEST_EFFORT, base::MayBlock()},
      base::BindOnce(&Job::Start, base::Unretained(job_ptr),
                     base::BindPostTask(self->sequenced_task_runner_,
                                        std::move(completion_cb))
                         .Then(base::BindOnce([](Job::SmartPtr<Job> job) {},
                                              std::move(job)))));

  NotifyObservers(self, Notification::STARTED_JOB);
}

// static
void Scheduler::ClearQueue(base::WeakPtr<Scheduler> self) {
  if (!self) {
    return;
  }

  DCHECK_CALLED_ON_VALID_SEQUENCE(self->sequence_checker_);
  while (!self->jobs_queue_.empty()) {
    auto& job = self->jobs_queue_.front();
    job->Cancel(Status(error::RESOURCE_EXHAUSTED,
                       "Unable to process due to low system memory"));
    self->jobs_queue_.pop();
  }
}

#ifdef MEMORY_PRESSURE_LEVEL_ENABLED
// TODO(1174889) Currently unused, once resourced implements
// MemoryPressureLevels update. Also initialize JobSemaphorePool at
// TaskLimit::OFF instead of NORMAL, so that it is off until we know the
// memory pressure level.
void Scheduler::UpdateMemoryPressureLevel(
    base::MemoryPressureLevel memory_pressure_level) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  switch (memory_pressure_level) {
    case base::MemoryPressureLevel::MEMORY_PRESSURE_LEVEL_NONE:
      job_semaphore_->UpdateTaskLimit(TaskLimit::NORMAL);
      StartJobs(weak_ptr_factory_.GetWeakPtr());
      return;
    case base::MemoryPressureLevel::MEMORY_PRESSURE_LEVEL_MODERATE:
      job_semaphore_->UpdateTaskLimit(TaskLimit::REDUCED);
      StartJobs(weak_ptr_factory_.GetWeakPtr());
      return;
    case base::MemoryPressureLevel::MEMORY_PRESSURE_LEVEL_CRITICAL:
      job_semaphore_->UpdateTaskLimit(TaskLimit::OFF);
      ClearQueue();
      return;
  }
}
#endif

}  // namespace reporting
