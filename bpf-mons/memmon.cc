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
#include "mons/bpf_skeletons/skeleton_memmon.bpf.h"

namespace {

static struct option long_options[] = {{"pid", required_argument, 0, 'p'},
                                       {0, 0, 0, 0}};

static int attach_probes(struct memmon_bpf* mon, pid_t pid) {
  LIBBPF_OPTS(bpf_uprobe_opts, uopts);
  std::string libc;

  if (libmon::lookup_lib(pid, "libc.so", libc))
    return -ENOENT;

  uopts.func_name = "malloc";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_malloc, &uopts);

  uopts.func_name = "malloc";
  uopts.retprobe = true;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), ret_malloc, &uopts);

  uopts.func_name = "strdup";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_strdup, &uopts);

  uopts.func_name = "strdup";
  uopts.retprobe = true;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), ret_strdup, &uopts);

  uopts.func_name = "calloc";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_calloc, &uopts);

  uopts.func_name = "calloc";
  uopts.retprobe = true;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), ret_calloc, &uopts);

  uopts.func_name = "memalign";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_memalign, &uopts);

  uopts.func_name = "memalign";
  uopts.retprobe = true;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), ret_memalign, &uopts);

  uopts.func_name = "mmap";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_mmap, &uopts);

  uopts.func_name = "mmap";
  uopts.retprobe = true;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), ret_mmap, &uopts);

  uopts.func_name = "munmap";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_munmap, &uopts);

  uopts.func_name = "free";
  uopts.retprobe = false;
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), call_free, &uopts);

  LIBBPF_OPTS(bpf_kprobe_opts, kopts);
  kopts.retprobe = false;
  LIBMON_ATTACH_KPROBE(mon, call_handle_mm_fault, "handle_mm_fault", &kopts);

  return 0;
}

static int handle_memmon_event(void* ctx, void* data, size_t data_sz) {
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

  libmon::decode_ustack(event->pid, event->ustack_ents, event->num_ustack_ents);
  return 0;
}

static int memmon(pid_t pid) {
  struct ring_buffer* rb = NULL;
  struct memmon_bpf* mon;
  int err;

  mon = memmon_bpf__open();
  if (!mon) {
    fprintf(stderr, "Failed to open BPF mon\n");
    return -EINVAL;
  }

  mon->rodata->kprobe_mon_pid = pid;
  err = memmon_bpf__load(mon);
  if (err) {
    fprintf(stderr, "Failed tp load BPF mon\n");
    goto cleanup;
  }

  err = attach_probes(mon, pid);
  if (err)
    goto cleanup;

  rb = ring_buffer__new(bpf_map__fd(mon->maps.rb), handle_memmon_event, NULL,
                        NULL);
  if (!rb) {
    fprintf(stderr, "Failed to open ring buffer\n");
    err = -EINVAL;
    goto cleanup;
  }

  err = libmon::setup_sig_handlers();
  if (err)
    goto cleanup;

  while (!libmon::should_stop()) {
    err = ring_buffer__poll(rb, LIBMON_RB_POLL_TIMEOUT);
    if (err == -EINTR) {
      err = 0;
      break;
    }
    if (err < 0) {
      printf("RB polling error: %d\n", err);
      break;
    }
  }

cleanup:
  ring_buffer__free(rb);
  memmon_bpf__destroy(mon);
  return err;
}

}  // namespace

int main(int argc, char** argv) {
  pid_t pid = -1;
  int c, ret;

  while (1) {
    int option_index = 0;

    c = getopt_long(argc, argv, "p:", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c) {
      case 'p':
        pid = std::stol(optarg);
        break;
      default:
        abort();
    }
  }

  ret = libmon::init_stack_decoder();
  if (ret)
    return ret;

  ret = memmon(pid);

  libmon::release_stack_decoder();
  return ret;
}
