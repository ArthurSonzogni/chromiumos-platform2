// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Runs mount-passthrough with minijail0 as chronos.
// mount-passthrough-jailed is in the process of being ported from shell
// to C++.

#include <base/logging.h>
#include <stdlib.h>
#include <unistd.h>

namespace {
// This is not 'constexpr' for now as it'll be passed to char* argv[].
char kShellScriptPath[] = "/usr/bin/mount-passthrough-jailed.sh";
}  // namespace

int main(int argc, char* argv[]) {
  argv[0] = kShellScriptPath;
  execve(argv[0], argv, environ);
  PLOG(ERROR) << "execve failed with " << argv[0];
  return EXIT_FAILURE;  // execve() failed.
}
