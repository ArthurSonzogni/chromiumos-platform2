// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_SCHEDULER_SCHEDULER_H_
#define MISSIVE_SCHEDULER_SCHEDULER_H_

#include <memory>
#include <vector>

#include <base/sequence_checker.h>
#include <base/sequenced_task_runner.h>

#include "missive/util/shared_queue.h"
#include "missive/util/status.h"

namespace reporting {

// Scheduler manages the running jobs ensuring that we don't overload the
// system memory. It runs in three modes:
// 1. NORMAL: In normal mode Scheduler will schedule up to 5 concurrent jobs
//    keeping the rest in the |jobs_queue_|.
// 2. REDUCED: In reduced mode Scheduler will schedule up to 2 concurrent jobs,
//    although any currently running jobs are allowed to finish.
// 3. OFF: In this mode Scheduler will enqueue no new jobs, all currently
//    running jobs are allowed to finish. Jobs in the |jobs_queue_| will be
//    cancelled.
class Scheduler {
 public:
  // A Job is a unit of work with a common interface. |StartImpl| needs to be
  // overridden to implement the specific job functionality ending up calling
  // |Finish|. To protect work from being corrupted, most of the public
  // functions only work when the job is in the NOT_RUNNING state.
  class Job {
   public:
    // NOT_RUNNING is the inital state of a Job, no methods have been called and
    // it is waiting for the Scheduler to Start it.
    // RUNNING is a protected state for the job, only the job can move it to
    // another state.
    // COMPLETED is a successful terminal state for the job.
    // CANCELLED is an unsuccessful terminal state for the job.
    enum class JobState { NOT_RUNNING, RUNNING, COMPLETED, CANCELLED };

    // JobDelegate is responsible for sending responses to any
    // listeners.
    class JobDelegate {
     public:
      JobDelegate() = default;
      virtual ~JobDelegate() = default;

     private:
      // Job should be the only class calling Complete or Cancel;
      friend Job;

      // Comple and Cancel will be called by Job::Finish and should notify
      // listeners of the Jobs completion.
      virtual Status Complete() = 0;
      virtual Status Cancel(Status status) = 0;
    };

    explicit Job(std::unique_ptr<JobDelegate> job_response_delegate);
    virtual ~Job() = default;

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    // If the job is not currently NOT_RUNNING, will simply return.
    void Start(base::OnceCallback<void(Status)> complete_cb);

    // If the job is not currently NOT_RUNNING, will simply return.
    // Cancel move the job to the CANCELLED state and call |cancel_callback_|
    // with the provided Status.
    // Job cannot be started after a cancellation, so care must be taken to only
    // cancel when appropriate.
    Status Cancel(Status status);

    // Returns the |job_state_| at the time of calling.
    JobState GetJobState() const;

   protected:
    // StartImpl should perform the unit of work for the Job and call Finish
    // upon completion.
    virtual void StartImpl() = 0;

    // Finish will call either report_completion_callback_ or cancel_callback_
    // based on the provided status. In addition it will also update job_state_
    // appropriately.
    void Finish(Status status);

    std::unique_ptr<JobDelegate> job_response_delegate_;

   private:
    std::atomic<JobState> job_state_{JobState::NOT_RUNNING};

    // |complete_cb_| is set by |Start| and called by |Finish|.
    base::OnceCallback<void(Status)> complete_cb_;
  };

  // SchedulerObserver allows introspection into the goings on of the Scheudler.
  class SchedulerObserver {
   public:
    enum class Notification {
      // A job has successfully been enqueued.
      ACCEPTED_JOB,

      // A job was rejected from enqueuing, and cancelled.
      REJECTED_JOB,

      // A job attempted to acquire a JobBlocker and was unable to do so.
      BLOCKED_JOB,

      // A job was started.
      STARTED_JOB,

      // Set if a job is successfully completed.
      SUCCESSFUL_COMPLETION,

      // Set if a job was unsuccessful in completion.
      UNSUCCESSFUL_COMPLETION,

      // A job was cancelled due to memory pressure.
      MEMORY_PRESSURE_CANCELLATION,
    };

    SchedulerObserver() = default;
    ~SchedulerObserver() = default;

    SchedulerObserver(const SchedulerObserver& other) = delete;
    SchedulerObserver& operator=(const SchedulerObserver& other) = delete;

    virtual void Notify(Notification notification) = 0;
  };

  Scheduler();
  ~Scheduler();

  void AddObserver(SchedulerObserver* observer);
  void NotifyObservers(SchedulerObserver::Notification notification);

  // EnqueueJob will store the job in the |job_queue_|, and it will be executed
  // as long as system memory remains above CRITICAL.
  void EnqueueJob(std::unique_ptr<Job> job);

 private:
  class JobContext;
  class JobBlocker;
  class JobSemaphore;

  void OnJobEnqueued();

  void StartJobs();
  void OnJobPop(std::unique_ptr<JobBlocker> job_blocker,
                StatusOr<std::unique_ptr<Job>> job_result);

  void ClearQueue();
  void OnJobQueueSwap(base::queue<std::unique_ptr<Job>> job_queue) const;

  // TODO(1174889) Currently unused, once resourced implements
  // MemoryPressureLevels update. Also initialize JobSemaphorePool at
  // TaskLimit::OFF instead of NORMAL, so that it is off until we know the
  // memory pressure level.
  // void UpdateMemoryPressureLevel(
  //     base::MemoryPressureListener::MemoryPressureLevel
  //     memory_pressure_level);

  // Must be the first member of the class.
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner_;
  SEQUENCE_CHECKER(sequence_checker_);

  std::unique_ptr<JobSemaphore> job_semaphore_;
  scoped_refptr<SharedQueue<std::unique_ptr<Job>>> jobs_queue_;

  std::vector<SchedulerObserver*> observers_;
};

}  // namespace reporting

#endif  // MISSIVE_SCHEDULER_SCHEDULER_H_
