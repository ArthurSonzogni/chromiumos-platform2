// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_GENERATOR_JOB_H_
#define LOGIN_MANAGER_GENERATOR_JOB_H_

#include "login_manager/child_job.h"
#include "login_manager/subprocess.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>

namespace login_manager {

class SystemUtils;

class GeneratorJobInterface : public ChildJobInterface {};

class GeneratorJobFactoryInterface {
 public:
  virtual ~GeneratorJobFactoryInterface();
  virtual std::unique_ptr<GeneratorJobInterface> Create(
      const std::string& filename,
      const base::FilePath& user_path,
      uid_t desired_uid,
      SystemUtils* utils) = 0;
};

class GeneratorJob : public GeneratorJobInterface {
 public:
  class Factory : public GeneratorJobFactoryInterface {
   public:
    Factory();
    ~Factory() override;
    std::unique_ptr<GeneratorJobInterface> Create(
        const std::string& filename,
        const base::FilePath& user_path,
        uid_t desired_uid,
        SystemUtils* utils) override;

   private:
    DISALLOW_COPY_AND_ASSIGN(Factory);
  };

  ~GeneratorJob() override;

  // Overridden from GeneratorJobInterface
  bool RunInBackground() override;
  void KillEverything(int signal, const std::string& message) override;
  void Kill(int signal, const std::string& message) override;
  void WaitAndAbort(base::TimeDelta timeout) override;
  const std::string GetName() const override;
  pid_t CurrentPid() const override;

 private:
  GeneratorJob(const std::string& filename,
               const base::FilePath& user_path,
               uid_t desired_uid,
               SystemUtils* utils);

  // Fully-specified name for generated key file.
  const std::string filename_;
  // Fully-specified path for the user's home.
  const std::string user_path_;

  // Wrapper for system library calls. Externally owned.
  SystemUtils* system_;

  // The subprocess tracked by this job.
  Subprocess subprocess_;

  DISALLOW_COPY_AND_ASSIGN(GeneratorJob);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_GENERATOR_JOB_H_
