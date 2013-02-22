// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/minijail.h"

#include <sys/types.h>
#include <sys/wait.h>

using std::vector;

namespace shill {

static base::LazyInstance<Minijail> g_minijail = LAZY_INSTANCE_INITIALIZER;

Minijail::Minijail() {}

Minijail::~Minijail() {}

// static
Minijail *Minijail::GetInstance() {
  return g_minijail.Pointer();
}

struct minijail *Minijail::New() {
  return minijail_new();
}

void Minijail::Destroy(struct minijail *jail) {
  minijail_destroy(jail);
}

bool Minijail::DropRoot(struct minijail *jail, const char *user) {
  // |user| is copied so the only reason either of these calls can fail
  // is ENOMEM.
  return !minijail_change_user(jail, user) &&
         !minijail_change_group(jail, user);
}

void Minijail::UseCapabilities(struct minijail *jail, uint64_t capmask) {
  minijail_use_caps(jail, capmask);
}

bool Minijail::Run(struct minijail *jail,
                   vector<char *> args, pid_t *pid) {
  return minijail_run_pid(jail, args[0], args.data(), pid) == 0;
}

bool Minijail::RunSync(struct minijail *jail,
                       vector<char *> args, int *status) {
  pid_t pid;
  if (Run(jail, args, &pid) && waitpid(pid, status, 0) == pid) {
    return true;
  }

  return false;
}

bool Minijail::RunPipe(struct minijail *jail,
                       vector<char *> args, pid_t *pid, int *stdin) {
  return minijail_run_pid_pipe(jail, args[0], args.data(), pid, stdin) == 0;
}

bool Minijail::RunPipes(struct minijail *jail, vector<char *> args, pid_t *pid,
                        int *stdin, int *stdout, int *stderr) {
  return minijail_run_pid_pipes(jail, args[0], args.data(),
                                pid, stdin, stdout, stderr) == 0;
}

bool Minijail::RunAndDestroy(struct minijail *jail,
                             vector<char *> args, pid_t *pid) {
  bool res = Run(jail, args, pid);
  Destroy(jail);
  return res;
}

bool Minijail::RunSyncAndDestroy(struct minijail *jail,
                                 vector<char *> args, int *status) {
  bool res = RunSync(jail, args, status);
  Destroy(jail);
  return res;
}

bool Minijail::RunPipeAndDestroy(struct minijail *jail,
                                 vector<char *> args, pid_t *pid, int *stdin) {
  bool res = RunPipe(jail, args, pid, stdin);
  Destroy(jail);
  return res;
}

bool Minijail::RunPipesAndDestroy(struct minijail *jail,
                                  vector<char *> args, pid_t *pid, int *stdin,
                                  int *stdout, int *stderr) {
  bool res = RunPipes(jail, args, pid, stdin, stdout, stderr);
  Destroy(jail);
  return res;
}

}  // namespace shill
