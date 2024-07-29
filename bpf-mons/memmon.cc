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
#include <time.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "include/libmon.h"
// This include must be after stdint.h
#include "include/memmon.h"
#include "include/memmon_tracing.h"
#include "mons/bpf_skeletons/skeleton_memmon.bpf.h"

namespace {

static struct option long_options[] = {{"pid", required_argument, 0, 'p'},
                                       {"exec", required_argument, 0, 'e'},
                                       {"mode", required_argument, 0, 'm'},
                                       {0, 0, 0, 0}};

enum run_modes {
  RUN_MODE_INVALID,
  RUN_MODE_STDOUT,
  RUN_MODE_PERFETTO,
  RUN_MODE_LEAKCHECK,
};

static int run_mode = RUN_MODE_STDOUT;
static std::unordered_map<uint64_t, struct memmon_event*> events;

typedef int (*event_handler_t)(void* ctx, void* data, size_t data_sz);

static int attach_probes(struct memmon_bpf* mon, pid_t pid) {
  std::string libc;

  if (libmon::lookup_lib(pid, "libc.so", libc))
    return -ENOENT;

  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "malloc", call_malloc);
  LIBMON_ATTACH_URETPROBE(mon, pid, libc.c_str(), "malloc", ret_malloc);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "strdup", call_strdup);
  LIBMON_ATTACH_URETPROBE(mon, pid, libc.c_str(), "strdup", ret_strdup);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "calloc", call_calloc);
  LIBMON_ATTACH_URETPROBE(mon, pid, libc.c_str(), "calloc", ret_calloc);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "memalign", call_memalign);
  LIBMON_ATTACH_URETPROBE(mon, pid, libc.c_str(), "memalign", ret_memalign);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "mmap", call_mmap);
  LIBMON_ATTACH_URETPROBE(mon, pid, libc.c_str(), "mmap", ret_mmap);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "munmap", call_munmap);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "free", call_free);
  LIBMON_ATTACH_KPROBE(mon, "handle_mm_fault", call_handle_mm_fault);
  return 0;
}

static int perfetto_memmon_event(void* ctx, void* data, size_t data_sz) {
  struct memmon_event* event = (struct memmon_event*)data;

  if (event->type == MEMMON_EVENT_MALLOC || event->type == MEMMON_EVENT_MMAP ||
      event->type == MEMMON_EVENT_STRDUP ||
      event->type == MEMMON_EVENT_CALLOC ||
      event->type == MEMMON_EVENT_MEMALIGN) {
    std::vector<std::string> bt;
    std::string frames;

    libmon::decode_ustack(event->pid, event->ustack_ents,
                          event->num_ustack_ents, bt);
    for (auto& frame : bt)
      frames += frame + "\n";

    MEMMON_EVENT_BEGIN("mm", memmon_event_track(event), "fn",
                       memmon_event_name(event), "call trace", frames.c_str());
  }

  if (event->type == MEMMON_EVENT_FREE || event->type == MEMMON_EVENT_MUNMAP) {
    if (event->ptr == 0x00)
      return 0;
    MEMMON_EVENT_END(memmon_event_track(event));
  }
  return 0;
}

static int stdout_memmon_event(void* ctx, void* data, size_t data_sz) {
  struct memmon_event* event = (struct memmon_event*)data;

  printf("comm: %s pid:%d event: ", event->comm, event->pid);
  switch (event->type) {
    case MEMMON_EVENT_MALLOC:
      printf("malloc() sz=%lu ptr=%p-%p\n", event->size,
             reinterpret_cast<void*>(event->ptr),
             reinterpret_cast<void*>(event->ptr + event->size));
      break;
    case MEMMON_EVENT_FREE:
      printf("free() ptr=%p\n", reinterpret_cast<void*>(event->ptr));
      break;
    case MEMMON_EVENT_MMAP:
      printf("mmap() sz=%lu ptr=%p-%p\n", event->size,
             reinterpret_cast<void*>(event->ptr),
             reinterpret_cast<void*>(event->ptr + event->size));
      break;
    case MEMMON_EVENT_MUNMAP:
      printf("munmap() ptr=%p\n", reinterpret_cast<void*>(event->ptr));
      break;
    case MEMMON_EVENT_STRDUP:
      printf("strdup() ptr=%p -> ptr=%p\n",
             reinterpret_cast<void*>(event->size),
             reinterpret_cast<void*>(event->ptr));
      break;
    case MEMMON_EVENT_CALLOC:
      printf("calloc() sz=%lu ptr=%p-%p\n", event->size,
             reinterpret_cast<void*>(event->ptr),
             reinterpret_cast<void*>(event->ptr + event->size));
      break;
    case MEMMON_EVENT_MEMALIGN:
      printf("memalign() sz=%lu ptr=%p-%p\n", event->size,
             reinterpret_cast<void*>(event->ptr),
             reinterpret_cast<void*>(event->ptr + event->size));
      break;
    case MEMMON_EVENT_PF:
      printf("handle_mm_fault() ptr=%p\n", reinterpret_cast<void*>(event->ptr));
      break;

    case MEMMON_EVENT_INVALID:
      printf("INVALID\n");
      return -EINVAL;
  }

  libmon::show_ustack(event->pid, event->ustack_ents, event->num_ustack_ents);
  return 0;
}

static int leakcheck_memmon_event(void* ctx, void* data, size_t data_sz) {
  struct memmon_event* event = (struct memmon_event*)data;
  struct memmon_event* ev;

  if (event->ptr == 0x00)
    return 0;

  switch (event->type) {
    case MEMMON_EVENT_MALLOC:
    case MEMMON_EVENT_MMAP:
    case MEMMON_EVENT_STRDUP:
    case MEMMON_EVENT_CALLOC:
    case MEMMON_EVENT_MEMALIGN:
      if (events[event->ptr] != NULL) {
        printf("Missed free event for ptr %p?\n",
               reinterpret_cast<void*>(event->ptr));
        delete events[event->ptr];
        events[event->ptr] = NULL;
      }

      ev = new struct memmon_event;
      memcpy(ev, event, sizeof(*ev));
      events[event->ptr] = ev;
      break;
    case MEMMON_EVENT_FREE:
    case MEMMON_EVENT_MUNMAP:
      if (events[event->ptr]) {
        delete events[event->ptr];
        events[event->ptr] = NULL;
      }
      break;

    case MEMMON_EVENT_INVALID:
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
      struct memmon_event* ev = e.second;

      printf("still available memory %p\n", reinterpret_cast<void*>(e.first));
      libmon::show_ustack(ev->pid, ev->ustack_ents, ev->num_ustack_ents);
    }
  }
}

static int memmon(pid_t pid, const char* cmd, std::vector<char*>& cmd_args) {
  event_handler_t event_handler = stdout_memmon_event;
  struct ring_buffer* rb = NULL;
  struct memmon_bpf* mon;
  int err;

  mon = memmon_bpf__open();
  if (!mon) {
    fprintf(stderr, "Failed to open BPF mon\n");
    return -EINVAL;
  }

  err = libmon::prepare_target(pid, cmd, cmd_args);
  if (err)
    goto cleanup;

  mon->rodata->kprobe_mon_pid = pid;
  err = memmon_bpf__load(mon);
  if (err) {
    fprintf(stderr, "Failed to load BPF mon\n");
    goto cleanup;
  }

  err = attach_probes(mon, pid);
  if (err)
    goto cleanup;

  if (run_mode == RUN_MODE_PERFETTO) {
    event_handler = perfetto_memmon_event;
    memmon_tracing_init();
  }

  if (run_mode == RUN_MODE_LEAKCHECK)
    event_handler = leakcheck_memmon_event;

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
  printf("memmon status: %d\n", err);
  ring_buffer__free(rb);
  memmon_bpf__destroy(mon);
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

    c = getopt_long(argc, argv, "p:e:m:", long_options, &option_index);

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
      case 'm':
        if (!strcmp(optarg, "perfetto")) {
          run_mode = RUN_MODE_PERFETTO;
        } else if (!strcmp(optarg, "stdout")) {
          run_mode = RUN_MODE_STDOUT;
        } else if (!strcmp(optarg, "leakcheck")) {
          run_mode = RUN_MODE_LEAKCHECK;
        } else {
          printf("Invalid run mode: %s\n", optarg);
          return -EINVAL;
        }
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

  if (exec_cmd) {
    exec_args.push_back(basename(exec_cmd));
    while (optind < argc) {
      exec_args.push_back(argv[optind]);
      optind++;
    }
    exec_args.push_back(NULL);
  }

  ret = libmon::init_stack_decoder();
  if (ret)
    return ret;

  ret = memmon(pid, exec_cmd, exec_args);

  libmon::release_stack_decoder();
  return ret;
}
