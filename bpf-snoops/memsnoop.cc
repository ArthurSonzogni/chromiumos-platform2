// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <fcntl.h>
#include <getopt.h>
#include <linux/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "include/libsnoop.h"
#include "include/memsnoop.h"
#include "snoops/bpf_skeletons/skeleton_memsnoop.bpf.h"

namespace {

static struct option long_options[] = {{"pid", required_argument, 0, 'p'},
                                       {0, 0, 0, 0}};

static int attach_probes(struct memsnoop_bpf* snoop, int pid) {
  LIBBPF_OPTS(bpf_uprobe_opts, uopts);
  std::string libc;

  if (libsnoop::lookup_lib("libc.so.6", libc))
    return -ENOENT;

  uopts.func_name = "malloc";
  uopts.retprobe = false;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), call_malloc, &uopts);

  uopts.func_name = "malloc";
  uopts.retprobe = true;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), ret_malloc, &uopts);

  uopts.func_name = "mmap";
  uopts.retprobe = false;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), call_mmap, &uopts);

  uopts.func_name = "mmap";
  uopts.retprobe = true;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), ret_mmap, &uopts);

  uopts.func_name = "munmap";
  uopts.retprobe = false;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), call_munmap, &uopts);

  uopts.func_name = "free";
  uopts.retprobe = false;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), call_free, &uopts);

  LIBBPF_OPTS(bpf_kprobe_opts, kopts);
  kopts.retprobe = false;
  LIBSNOOP_ATTACH_KPROBE(snoop, call_handle_mm_fault, "handle_mm_fault",
                         &kopts);

  return 0;
}

static int handle_memsnoop_event(void* ctx, void* data, size_t data_sz) {
  struct memsnoop_event* event = (struct memsnoop_event*)data;

  printf("comm: %s pid:%d event: ", event->comm, event->pid);
  switch (event->type) {
    case MEMSNOOP_EVENT_MALLOC:
      printf("malloc() sz=%llu ptr=%p-%p\n", event->size,
             reinterpret_cast<void*>(event->ptr),
             reinterpret_cast<void*>(event->ptr + event->size));
      break;
    case MEMSNOOP_EVENT_FREE:
      printf("free() ptr=%p\n", reinterpret_cast<void*>(event->ptr));
      break;
    case MEMSNOOP_EVENT_MMAP:
      printf("mmap() sz=%llu ptr=%p-%p\n", event->size,
             reinterpret_cast<void*>(event->ptr),
             reinterpret_cast<void*>(event->ptr + event->size));
      break;
    case MEMSNOOP_EVENT_MUNMAP:
      printf("munmap() ptr=%p\n", reinterpret_cast<void*>(event->ptr));
      break;
    case MEMSNOOP_EVENT_PF:
      printf("handle_mm_fault() ptr=%p\n", reinterpret_cast<void*>(event->ptr));
      break;

    case MEMSNOOP_EVENT_INVALID:
      printf("INVALID\n");
      return -EINVAL;
  }

  libsnoop::decode_ustack(event->pid, event->ustack_ents,
                          event->num_ustack_ents);
  return 0;
}

static int memsnoop(pid_t pid) {
  struct ring_buffer* rb = NULL;
  struct memsnoop_bpf* snoop;
  int err;

  snoop = memsnoop_bpf__open();
  if (!snoop) {
    fprintf(stderr, "Failed to open BPF snoop\n");
    return -EINVAL;
  }

  snoop->rodata->kprobe_snoop_pid = pid;
  err = memsnoop_bpf__load(snoop);
  if (err) {
    fprintf(stderr, "Failed tp load BPF snoop\n");
    goto cleanup;
  }

  err = attach_probes(snoop, pid);
  if (err)
    goto cleanup;

  rb = ring_buffer__new(bpf_map__fd(snoop->maps.rb), handle_memsnoop_event,
                        NULL, NULL);
  if (!rb) {
    fprintf(stderr, "Failed to open ring buffer\n");
    err = -EINVAL;
    goto cleanup;
  }

  while ((err = ring_buffer__poll(rb, -1)) >= 0) {
  }

cleanup:
  ring_buffer__free(rb);
  memsnoop_bpf__destroy(snoop);
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

  ret = libsnoop::init_stack_decoder();
  if (ret)
    return ret;

  ret = memsnoop(pid);

  libsnoop::release_stack_decoder();
  return ret;
}
