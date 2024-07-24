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
                                       {0, 0, 0, 0}};

static int attach_probes(struct fdmon_bpf* mon, int pid) {
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

static int fdmon(pid_t pid) {
  struct ring_buffer* rb = NULL;
  struct fdmon_bpf* mon;
  int err;

  mon = fdmon_bpf__open();
  if (!mon) {
    fprintf(stderr, "Failed to open BPF mon\n");
    return -EINVAL;
  }

  err = fdmon_bpf__load(mon);
  if (err) {
    fprintf(stderr, "Failed tp load BPF mon\n");
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

  while ((err = ring_buffer__poll(rb, -1)) >= 0) {
  }

cleanup:
  ring_buffer__free(rb);
  fdmon_bpf__destroy(mon);
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

  ret = fdmon(pid);
  libmon::release_stack_decoder();

  return ret;
}
