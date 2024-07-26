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
#include <unordered_map>

// This include must be after stdint.h
#include "include/fdmon.h"
#include "include/libmon.h"
#include "mons/bpf_skeletons/skeleton_fdmon.bpf.h"

namespace {

static struct option long_options[] = {{"pid", required_argument, 0, 'p'},
                                       {"exec", required_argument, 0, 'e'},
                                       {"leakcheck", no_argument, 0, 'l'},
                                       {0, 0, 0, 0}};

enum run_modes {
  RUN_MODE_INVALID,
  RUN_MODE_STDOUT,
  RUN_MODE_LEAKCHECK,
};

static int run_mode = RUN_MODE_STDOUT;
static std::unordered_map<int32_t, struct fdmon_event*> events;

typedef int (*event_handler_t)(void* ctx, void* data, size_t data_sz);

static int attach_probes(struct fdmon_bpf* mon, pid_t pid) {
  std::string libc;

  if (libmon::lookup_lib(pid, "libc.so", libc))
    return -ENOENT;

  LIBMON_ATTACH_URETPROBE(mon, pid, libc.c_str(), "open", ret_open);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "dup2", call_dup2);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "dup", call_dup);
  LIBMON_ATTACH_URETPROBE(mon, pid, libc.c_str(), "dup", ret_dup);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "close", call_close);
  return 0;
}

static int stdout_fdmon_event(void* ctx, void* data, size_t data_sz) {
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

static int leakcheck_fdmon_event(void* ctx, void* data, size_t data_sz) {
  struct fdmon_event* event = (struct fdmon_event*)data;
  struct fdmon_event* ev;

  if (event->nfd < 0)
    return 0;

  switch (event->type) {
    case FDMON_EVENT_OPEN:
    case FDMON_EVENT_DUP:
      if (events[event->nfd] != NULL) {
        printf("Missed close() event for fd %d?\n", event->nfd);
        delete events[event->nfd];
        events[event->nfd] = NULL;
      }

      ev = new struct fdmon_event;
      memcpy(ev, event, sizeof(*ev));
      events[event->nfd] = ev;
      break;
    case FDMON_EVENT_CLOSE:
      if (events[event->nfd]) {
        delete events[event->nfd];
        events[event->nfd] = NULL;
      }
      break;

    case FDMON_EVENT_INVALID:
      printf("INVALID\n");
      return -EINVAL;
  }

  return 0;
}

static void show_leakcheck(void) {
  if (run_mode != RUN_MODE_LEAKCHECK)
    return;

  for (auto& e : events) {
    if (e.second) {
      struct fdmon_event* ev = e.second;

      printf("still available file-descriptor %d\n", e.first);
      libmon::decode_ustack(ev->pid, ev->ustack_ents, ev->num_ustack_ents);
    }
  }
}

static int fdmon(pid_t pid, const char* cmd, std::vector<char*>& args) {
  event_handler_t event_handler = stdout_fdmon_event;
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

  if (run_mode == RUN_MODE_LEAKCHECK)
    event_handler = leakcheck_fdmon_event;

  rb = ring_buffer__new(bpf_map__fd(mon->maps.rb), event_handler, NULL, NULL);
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

  show_leakcheck();

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

    c = getopt_long(argc, argv, "p:e:l", long_options, &option_index);

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
      case 'l':
        run_mode = RUN_MODE_LEAKCHECK;
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
