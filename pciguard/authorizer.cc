// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pciguard/authorizer.h"

#include <sysexits.h>

#include <base/logging.h>

namespace pciguard {

void* Authorizer::AuthorizerThread(void* ptr) {
  Authorizer* authorizer = static_cast<Authorizer*>(ptr);
  Job job;
  while (authorizer->GetNextJob(&job)) {
    if (job.type_ == AUTHORIZE_ALL_DEVICES) {
      authorizer->utils_->AuthorizeAllDevices();
    } else {
      authorizer->utils_->AuthorizeThunderboltDev(job.syspath_);
    }

    pthread_mutex_lock(&authorizer->mutex_);
    authorizer->authorization_in_flight_ = false;
    pthread_mutex_unlock(&authorizer->mutex_);
  }
  return NULL;
}

Authorizer::Authorizer(SysfsUtils* utils)
    : mutex_(PTHREAD_MUTEX_INITIALIZER),
      job_available_(PTHREAD_COND_INITIALIZER),
      authorization_in_flight_(false),
      should_stop_(false),
      utils_(utils) {
  if (pthread_create(&authorizer_thread_, NULL, &Authorizer::AuthorizerThread,
                     this)) {
    PLOG(ERROR) << __func__ << ": Problem creating thread. Exiting now";
    exit(EX_OSERR);
  }
  LOG(INFO) << "Created new authorizer object";
}

Authorizer::~Authorizer() {
  if (pthread_mutex_lock(&mutex_) == 0) {
    should_stop_ = true;
    pthread_cond_signal(&job_available_);
    pthread_mutex_unlock(&mutex_);
  }
  pthread_join(authorizer_thread_, NULL);
  LOG(INFO) << "Destroyed authorizer object";
}

void Authorizer::SubmitJob(JobType type, base::FilePath path) {
  Job job = {type, path};

  if (pthread_mutex_lock(&mutex_)) {
    PLOG(ERROR) << "Mutex lock issue while submitting job";
    return;
  }
  queue_.push(job);
  LOG(INFO) << "Inserted authorization job (" << queue_.back().type_ << ","
            << queue_.back().syspath_ << ")";
  pthread_cond_signal(&job_available_);

  if (pthread_mutex_unlock(&mutex_)) {
    PLOG(ERROR) << "Mutex unlock issue while submitting job";
    return;
  }
}

bool Authorizer::IsJobQueueEmpty() {
  if (pthread_mutex_lock(&mutex_)) {
    PLOG(ERROR) << "Mutex lock issue while checking job queue status";
    exit(EXIT_FAILURE);
  }
  auto ret = queue_.empty() && !authorization_in_flight_;
  if (pthread_mutex_unlock(&mutex_)) {
    PLOG(ERROR) << "Mutex unlock issue while checking job queue status";
    exit(EXIT_FAILURE);
  }
  return ret;
}

// Pops and returns next authorization job. If no job, then blocks until
// next job is available
bool Authorizer::GetNextJob(Job* job) {
  if (pthread_mutex_lock(&mutex_)) {
    PLOG(ERROR) << "Mutex lock issue while retrieving job";
    return false;
  }

  while (queue_.empty() && !should_stop_) {
    pthread_cond_wait(&job_available_, &mutex_);
  }

  if (should_stop_) {
    pthread_mutex_unlock(&mutex_);
    return false;
  }

  *job = queue_.front();
  queue_.pop();
  LOG(INFO) << "Fetched authorization job (" << job->type_ << ","
            << job->syspath_ << ")";

  authorization_in_flight_ = true;

  if (pthread_mutex_unlock(&mutex_)) {
    PLOG(ERROR) << "Mutex unlock issue while retrieving job";
  }
  return true;
}

}  // namespace pciguard
