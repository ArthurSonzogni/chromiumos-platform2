// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple program used to test interaction with update_engine when
// executing other programs. This program receives pre-programmed actions in the
// command line and executes them in order.

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#define EX_USAGE_ERROR 100

void usage(const char* program, const char* error) {
  if (error)
    fprintf(stderr, "ERROR: %s\n", error);
  fprintf(stderr, "Usage: %s <cmd> [args..]\n", program);
  exit(EX_USAGE_ERROR);
}

int main(int argc, char** argv, char** envp) {
  if (argc < 2)
    usage(argv[0], "No command passed");

  std::string cmd(argv[1]);
  if (cmd == "fstat") {
    // Call fstat on the passed file descriptor number
    if (argc < 3)
      usage(argv[0], "No fd passed to fstat");
    int fd = atoi(argv[2]);
    struct stat buf;
    int rc = fstat(fd, &buf);
    if (rc < 0) {
      int ret = errno;
      perror("fstat");
      return ret;
    }
    return 0;
  }

  usage(argv[0], "Unknown command");
}
