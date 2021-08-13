// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "missive/scheduler/scheduler.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/sequence_checker.h>
#include <base/sequenced_task_runner.h>
#include <base/task/thread_pool.h>
#include <base/threading/sequenced_task_runner_handle.h>

#include "missive/util/shared_queue.h"
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
  DCHECK(complete_cb);
  // std::atomic<T>::compare_exchange_strong expects an lvalue for the expected
  // state.
  JobState expected_job_state = JobState::NOT_RUNNING;
  if (!job_state_.compare_exchange_strong(expected_job_state,
                                          /*desired=*/JobState::RUNNING)) {
    std::move(complete_cb)
        .Run(Status(
            error::UNAVAILABLE,
            "Job can only be started when it is in the NOT_RUNNING state."));
    return;
  }

  complete_cb_ = std::move(complete_cb);
  StartImpl();
}

Status Scheduler::Job::Cancel(Status status) {
  if (status.ok()) {
    return Status(error::INVALID_ARGUMENT,
                  "Job cannot be cancelled with an OK Status");
  }

  // std::atomic<T>::compare_exchange_strong expects an lvalue for the expected
  // state.
  JobState expected_job_state = JobState::NOT_RUNNING;
  if (!job_state_.compare_exchange_strong(expected_job_state,
                                          JobState::CANCELLED)) {
    return Status(error::UNAVAILABLE,
                  "Job cannot be cancelled after it has started.");
  }

  return job_response_delegate_->Cancel(status);
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

class Scheduler::JobContext : public TaskRunnerContext<CompleteJobResponse> {
 public:
  JobContext(Job::SmartPtr<Job> job,
             std::unique_ptr<JobBlocker> job_blocker,
             CompleteJobCallback job_completion_callback,
             scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner)
      : TaskRunnerContext<CompleteJobResponse>(
            std::move(job_completion_callback), sequenced_task_runner),
        job_(std::move(job)),
        job_blocker_(std::move(job_blocker)) {}

 private:
  ~JobContext() override { DCHECK(job_); }

  void OnStart() override {
    if (job_ == nullptr) {
      Status(error::INVALID_ARGUMENT, "Provided Job was null");
      return;
    }
    if (job_blocker_ == nullptr) {
      Complete(
          Status(error::INTERNAL, "Unable to process request at this time."));
      return;
    }
    job_->Start(base::BindOnce(&JobContext::Complete, base::Unretained(this)));
  }

  void Complete(Status status) {
    Schedule(&Scheduler::JobContext::Response, base::Unretained(this), status);
  }

  Job::SmartPtr<Scheduler::Job> job_;

  // Will be held until Response() is called, the semaphore will be released
  // once it is destroyed.
  std::unique_ptr<Scheduler::JobBlocker> job_blocker_;
};

class Scheduler::JobBlocker {
 public:
  JobBlocker(scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
             base::OnceCallback<void()> release_cb)
      : sequenced_task_runner_(sequenced_task_runner),
        release_cb_(std::move(release_cb)) {}

  ~JobBlocker() {
    sequenced_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::OnceCallback<void()> release_cb) {
                         std::move(release_cb).Run();
                       },
                       std::move(release_cb_)));
  }

  JobBlocker(const JobBlocker&) = delete;
  JobBlocker& operator=(const JobBlocker&) = delete;

 private:
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  base::OnceCallback<void()> release_cb_;
};

class Scheduler::JobSemaphore {
 public:
  JobSemaphore(scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
               TaskLimit task_limit)
      : sequenced_task_runner_(sequenced_task_runner), task_limit_(task_limit) {
    DETACH_FROM_SEQUENCE(sequence_checker_);
  }

  ~JobSemaphore() {
    if (running_jobs_ > 0) {
      LOG(ERROR) << "JobSemaphore destructing with active jobs.";
    }
  }

  JobSemaphore(const JobSemaphore&) = delete;
  JobSemaphore& operator=(const JobSemaphore&) = delete;

  StatusOr<std::unique_ptr<JobBlocker>> AcquireJobBlocker() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (running_jobs_ >= task_limit_) {
      return Status(error::RESOURCE_EXHAUSTED, "Currently at job limit");
    }
    running_jobs_++;

    return std::make_unique<JobBlocker>(
        sequenced_task_runner_,
        base::BindOnce(&JobSemaphore::Release, base::Unretained(this)));
  }

  void UpdateTaskLimit(TaskLimit task_limit) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    task_limit_ = task_limit;
  }

  bool IsAcceptingJobs() const { return task_limit_ != TaskLimit::OFF; }

 private:
  void Release() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    running_jobs_--;
  }

  SEQUENCE_CHECKER(sequence_checker_);

  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;

  std::atomic<TaskLimit> task_limit_{TaskLimit::OFF};
  uint32_t running_jobs_{0};
};

Scheduler::Scheduler()
    : sequenced_task_runner_(base::ThreadPool::CreateSequencedTaskRunner({})),
      job_semaphore_(std::make_unique<JobSemaphore>(sequenced_task_runner_,
                                                    TaskLimit::NORMAL)),
      jobs_queue_(
          SharedQueue<Job::SmartPtr<Job>>::Create(sequenced_task_runner_)) {}

Scheduler::~Scheduler() = default;

void Scheduler::AddObserver(SchedulerObserver* observer) {
  observers_.push_back(observer);
}

void Scheduler::NotifyObservers(Notification notification) {
  for (auto* observer : observers_) {
    observer->Notify(notification);
  }
}

void Scheduler::EnqueueJob(Job::SmartPtr<Job> job) {
  if (!job_semaphore_->IsAcceptingJobs()) {
    NotifyObservers(Notification::REJECTED_JOB);
    Status cancel_status =
        job->Cancel(Status(error::RESOURCE_EXHAUSTED,
                           "Unable to process due to low system memory"));
    if (!cancel_status.ok()) {
      LOG(ERROR) << "Was unable to successfully cancel a job: "
                 << cancel_status;
    }
    return;
  }
  jobs_queue_->Push(std::move(job), base::BindOnce(&Scheduler::OnJobEnqueued,
                                                   base::Unretained(this)));
}

void Scheduler::OnJobEnqueued() {
  NotifyObservers(Notification::ACCEPTED_JOB);
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&Scheduler::StartJobs, base::Unretained(this)));
}

void Scheduler::StartJobs() {
  DCHECK(base::SequencedTaskRunnerHandle::IsSet());

  // Get JobBlockers and assign them to jobs until job_semaphore_ returns a
  // non-OK status.
  StatusOr<std::unique_ptr<JobBlocker>> blocker_result =
      job_semaphore_->AcquireJobBlocker();
  while (blocker_result.ok()) {
    jobs_queue_->Pop(base::BindOnce(&Scheduler::OnJobPop,
                                    base::Unretained(this),
                                    std::move(blocker_result.ValueOrDie())));

    blocker_result = job_semaphore_->AcquireJobBlocker();
  }
  NotifyObservers(Notification::BLOCKED_JOB);
}

void Scheduler::OnJobPop(std::unique_ptr<JobBlocker> job_blocker,
                         StatusOr<Job::SmartPtr<Job>> job_result) {
  // job_result may be empty, if so just drop the request, releasing the
  // blocker.
  if (!job_result.ok()) {
    return;
  }

  auto completion_cb = base::BindOnce(
      [](scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner,
         base::OnceCallback<void()> start_jobs_cb,
         base::OnceCallback<void(Notification)> notify_observers,
         Status job_result) {
        sequenced_task_runner->PostTask(FROM_HERE, std::move(start_jobs_cb));

        if (!job_result.ok()) {
          std::move(notify_observers)
              .Run(Notification::UNSUCCESSFUL_COMPLETION);
          LOG(ERROR) << job_result;
          return;
        }
        std::move(notify_observers).Run(Notification::SUCCESSFUL_COMPLETION);
      },
      sequenced_task_runner_,
      base::BindOnce(&Scheduler::StartJobs, base::Unretained(this)),
      base::BindOnce(&Scheduler::NotifyObservers, base::Unretained(this)));

  Start<JobContext>(std::move(job_result.ValueOrDie()), std::move(job_blocker),
                    std::move(completion_cb), sequenced_task_runner_);
  NotifyObservers(Notification::STARTED_JOB);
}

void Scheduler::ClearQueue() {
  jobs_queue_->Swap(
      base::queue<Job::SmartPtr<Job>>(),
      base::BindOnce(&Scheduler::OnJobQueueSwap, base::Unretained(this)));
}

void Scheduler::OnJobQueueSwap(
    base::queue<Job::SmartPtr<Job>> job_queue) const {
  while (!job_queue.empty()) {
    auto& job = job_queue.front();
    Status cancel_status =
        job->Cancel(Status(error::RESOURCE_EXHAUSTED,
                           "Unable to process due to low system memory"));
    if (!cancel_status.ok()) {
      LOG(ERROR) << "Was unable to successfully cancel a job: "
                 << cancel_status;
    }
    job_queue.pop();
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
      StartJobs();
      return;
    case base::MemoryPressureLevel::MEMORY_PRESSURE_LEVEL_MODERATE:
      job_semaphore_->UpdateTaskLimit(TaskLimit::REDUCED);
      StartJobs();
      return;
    case base::MemoryPressureLevel::MEMORY_PRESSURE_LEVEL_CRITICAL:
      job_semaphore_->UpdateTaskLimit(TaskLimit::OFF);
      ClearQueue();
      return;
  }
}
#endif

}  // namespace reporting
