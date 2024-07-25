// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <string>

// This include must be after stdint.h
#include "include/fdmon.h"
#include "include/libmon.h"
#include "mons/bpf_skeletons/skeleton_fdmon.bpf.h"

namespace {

static struct option long_options[] = {{"pid", required_argument, 0, 'p'},
                                       {"exec", required_argument, 0, 'e'},
                                       {0, 0, 0, 0}};

static int attach_probes(struct fdmon_bpf* mon, pid_t pid) {
  LIBBPF_OPTS(bpf_uprobe_opts, uopts);
  std::string libc;

  if (libmon::lookup_lib(pid, "libc.so", libc))
    return -ENOENT;

  uopts.func_name = "open";
  uopts.retprobe = true;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), ret_open, &uopts);

  uopts.func_name = "dup2";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_dup2, &uopts);

  uopts.func_name = "dup";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_dup, &uopts);

  uopts.func_name = "dup";
  uopts.retprobe = true;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), ret_dup, &uopts);

  uopts.func_name = "close";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_close, &uopts);

  return 0;
}

static int handle_fdmon_event(void* ctx, void* data, size_t data_sz) {
  struct fdmon_event* event = (struct fdmon_event*)data;

  printf("comm: %s pid:%d event: ", event->comm, event->pid);
  switch (event->type) {
    case FDMON_EVENT_OPEN:
      printf("open() fd=%d\n", event->nfd);
      break;
    case FDMON_EVENT_DUP:
      printf("dup() fd=%d -> fd=%d\n", event->ofd, event->nfd);
      break;
    case FDMON_EVENT_CLOSE:
      printf("close() fd=%d\n", event->nfd);
      break;

    case FDMON_EVENT_INVALID:
      printf("INVALID\n");
      return -EINVAL;
  }

  libmon::decode_ustack(event->pid, event->ustack_ents, event->num_ustack_ents);
  return 0;
}

static int fdmon(pid_t pid, const char* cmd, std::vector<char*>& args) {
  struct ring_buffer* rb = NULL;
  struct fdmon_bpf* mon;
  int err;

  mon = fdmon_bpf__open();
  if (!mon) {
    fprintf(stderr, "Failed to open BPF mon\n");
    return -EINVAL;
  }

  err = libmon::prepare_target(pid, cmd, args);
  if (err)
    goto cleanup;

  err = fdmon_bpf__load(mon);
  if (err) {
    fprintf(stderr, "Failed to load BPF mon\n");
    goto cleanup;
  }

  err = attach_probes(mon, pid);
  if (err)
    goto cleanup;

  rb = ring_buffer__new(bpf_map__fd(mon->maps.rb), handle_fdmon_event, NULL,
                        NULL);
  if (!rb) {
    fprintf(stderr, "Failed to open ring buffer\n");
    err = -EINVAL;
    goto cleanup;
  }

  err = libmon::setup_sig_handlers();
  if (err)
    goto cleanup;

  err = libmon::follow_target(pid);
  if (err)
    goto cleanup;

  do {
    err = ring_buffer__poll(rb, LIBMON_RB_POLL_TIMEOUT);
    /* We should stop, no matter how many events are left in the rb */
    if (libmon::should_stop()) {
      err = 0;
      break;
    }
    if (err == -EINTR)
      continue;
    if (err < 0) {
      printf("rb polling error: %d\n", err);
      break;
    }
    /* Even if the target has terminated we still need to handle all events */
    if (err > 0)
      continue;
    if (libmon::target_terminated())
      break;
  } while (1);

cleanup:
  printf("fdmon status: %d\n", err);
  ring_buffer__free(rb);
  fdmon_bpf__destroy(mon);
  return err;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<char*> exec_args;
  char* exec_cmd = NULL;
  pid_t pid = -1;
  int c, ret;

  while (1) {
    int option_index = 0;

    c = getopt_long(argc, argv, "p:e:", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c) {
      case 'p':
        pid = std::stol(optarg);
        break;
      case 'e':
        exec_cmd = optarg;
        break;
      default:
        abort();
    }
  }

  if (pid != -1 && exec_cmd != NULL) {
    printf("Options -p and -e are mutually exclusive\n");
    return -EINVAL;
  }

  if (pid == -1 && exec_cmd == NULL) {
    printf("Must specify either -p or -e\n");
    return -EINVAL;
  }

  ret = libmon::init_stack_decoder();
  if (ret)
    return ret;

  if (exec_cmd) {
    exec_args.push_back(basename(exec_cmd));
    while (optind < argc) {
      exec_args.push_back(argv[optind]);
      optind++;
    }
    exec_args.push_back(NULL);
  }

  ret = fdmon(pid, exec_cmd, exec_args);

  libmon::release_stack_decoder();
  return ret;
}
