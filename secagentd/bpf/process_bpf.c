// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/secagentd/vmlinux/vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
// TODO(b/243453873): Workaround to get code completion working in CrosIDE.
#undef __cplusplus
#include "secagentd/bpf/process.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

const char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, MAX_STRUCT_SIZE * 1024);
} rb SEC(".maps");

SEC("lsm.s/bprm_committed_creds")
int BPF_PROG(handle_bprm_committed_creds, struct linux_binprm* binprm) {
  struct task_struct* task;
  struct event* e;
  struct process_start* p;
  // Reserve sample from BPF ringbuf.
  e = (struct event*)(bpf_ringbuf_reserve(&rb, sizeof(*e), 0));
  if (e == NULL) {
    return 0;
  }
  e->type = process_type;
  task = (struct task_struct*)bpf_get_current_task();
  e->data.process_event.type = process_start_type;
  p = &(e->data.process_event.data.process_start);

  // You can read lsm hook parameters without using bpf helpers.
  bpf_probe_read_kernel_str(p->filename, ARRAY_SIZE(p->filename),
                            binprm->filename);

  // Since this is process creation pid is equal to tgid.
  p->pid = bpf_get_current_pid_tgid() >> 32;

  p->spawn_namespace.mnt_ns = BPF_CORE_READ(task, nsproxy, mnt_ns, ns.inum);
  p->ppid = BPF_CORE_READ(task, real_parent, tgid);
  p->start_time = BPF_CORE_READ(task, start_boottime);
  p->parent_start_time = BPF_CORE_READ(task, real_parent, start_boottime);

  // Successfully submit it to user-space for post-processing.
  bpf_ringbuf_submit(e, 0);
  return 0;
}
