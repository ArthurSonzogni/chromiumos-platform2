// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_KEY_GENERATOR_H_
#define LOGIN_MANAGER_KEY_GENERATOR_H_

#include <signal.h>
#include <sys/types.h>

#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <base/time/time.h>

#include "login_manager/generator_job.h"
#include "login_manager/job_manager.h"

namespace login_manager {

class SystemUtils;

class KeyGenerator : public JobManagerInterface {
 public:
  class Delegate {
   public:
    virtual ~Delegate();
    virtual void OnKeyGenerated(const std::string& username,
                                const base::FilePath& temp_key_file) = 0;
  };

  KeyGenerator(uid_t uid, SystemUtils* utils);
  virtual ~KeyGenerator();

  void set_delegate(Delegate* delegate) { delegate_ = delegate; }

  // Start the generation of a new Owner keypair for |username| as |uid|.
  // Upon success, hands off ownership of the key generation job to |manager_|
  // and returns true.
  // The username of the key owner and temporary storage location of the
  // generated public key are stored internally until Reset() is called.
  virtual bool Start(const std::string& username);

  // Implementation of JobManagerInterface.
  bool IsManagedJob(pid_t pid) override;
  void HandleExit(const siginfo_t& status) override;
  void RequestJobExit() override;
  void EnsureJobExit(base::TimeDelta timeout) override;

  void InjectJobFactory(scoped_ptr<GeneratorJobFactoryInterface> factory);

 private:
  static const char kTemporaryKeyFilename[];

  // Clear per-generation state.
  void Reset();

  uid_t uid_;
  SystemUtils *utils_;
  Delegate* delegate_;

  scoped_ptr<GeneratorJobFactoryInterface> factory_;
  scoped_ptr<GeneratorJobInterface> keygen_job_;
  bool generating_;
  std::string key_owner_username_;
  std::string temporary_key_filename_;
  DISALLOW_COPY_AND_ASSIGN(KeyGenerator);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_KEY_GENERATOR_H_
