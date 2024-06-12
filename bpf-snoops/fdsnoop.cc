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
#include <unistd.h>

#include "include/fdsnoop.h"
#include "include/libsnoop.h"
#include "snoops/bpf_skeletons/skeleton_fdsnoop.bpf.h"

namespace {

static struct option long_options[] = {{"pid", required_argument, 0, 'p'},
                                       {0, 0, 0, 0}};

static int attach_probes(struct fdsnoop_bpf* snoop, int pid) {
  LIBBPF_OPTS(bpf_uprobe_opts, uopts);
  std::string libc;

  if (libsnoop::lookup_lib("libc.so.6", libc))
    return -ENOENT;

  uopts.func_name = "open";
  uopts.retprobe = true;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), ret_open, &uopts);

  uopts.func_name = "dup2";
  uopts.retprobe = false;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), call_dup2, &uopts);

  uopts.func_name = "dup";
  uopts.retprobe = false;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), call_dup, &uopts);

  uopts.func_name = "dup";
  uopts.retprobe = true;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), ret_dup, &uopts);

  uopts.func_name = "close";
  uopts.retprobe = false;
  LIBSNOOP_ATTACH_UPROBE(snoop, pid, libc.c_str(), call_close, &uopts);

  return 0;
}

static int handle_fdsnoop_event(void* ctx, void* data, size_t data_sz) {
  struct fdsnoop_event* event = (struct fdsnoop_event*)data;

  printf("comm: %s pid:%d event: ", event->comm, event->pid);
  switch (event->type) {
    case FDSNOOP_EVENT_OPEN:
      printf("open() fd=%d\n", event->nfd);
      break;
    case FDSNOOP_EVENT_DUP:
      printf("dup() fd=%d -> fd=%d\n", event->ofd, event->nfd);
      break;
    case FDSNOOP_EVENT_CLOSE:
      printf("close() fd=%d\n", event->nfd);
      break;

    case FDSNOOP_EVENT_INVALID:
      printf("INVALID\n");
      return -EINVAL;
  }

  libsnoop::decode_ustack(event->pid, event->ustack_ents,
                          event->num_ustack_ents);
  return 0;
}

static int fdsnoop(pid_t pid) {
  struct ring_buffer* rb = NULL;
  struct fdsnoop_bpf* snoop;
  int err;

  snoop = fdsnoop_bpf__open();
  if (!snoop) {
    fprintf(stderr, "Failed to open BPF snoop\n");
    return -EINVAL;
  }

  err = fdsnoop_bpf__load(snoop);
  if (err) {
    fprintf(stderr, "Failed tp load BPF snoop\n");
    goto cleanup;
  }

  err = attach_probes(snoop, pid);
  if (err)
    goto cleanup;

  rb = ring_buffer__new(bpf_map__fd(snoop->maps.rb), handle_fdsnoop_event, NULL,
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
  fdsnoop_bpf__destroy(snoop);
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


  ret = fdsnoop(pid);
  libsnoop::release_stack_decoder();

  return ret;
}
