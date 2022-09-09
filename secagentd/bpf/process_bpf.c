// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include vmlinux.h first to declare all kernel types.
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
#ifndef TRY_KPROBE
SEC("lsm/bprm_committed_creds")
int BPF_PROG(handle_committed_creds, struct linux_binprm* binprm) {
#else
SEC("kprobe/__SCT__tp_func_sched_process_exec")
int BPF_KPROBE(handle_sched_process_exec, struct linux_binprm* binprm) {
#endif
  struct task_struct* task;
  struct event* event;
  struct process_start* p;

  // Reserve sample from BPF ringbuf.
  event = (struct event*)(bpf_ringbuf_reserve(&rb, sizeof(*event), 0));
  if (event == NULL) {
    return 0;
  }
  event->type = process_type;
  task = (struct task_struct*)bpf_get_current_task();
  event->data.process_event.type = process_start_type;
  p = &(event->data.process_event.data.process_start);
#ifndef TRY_KPROBE
  // TODO: This doesn't work. Suspect that argv doesn't actually point to
  // anything yet.
  bpf_probe_read_user_str(p->command_line, ARRAY_SIZE(p->command_line),
                          (void*)binprm->vma->vm_mm->arg_start);

  bpf_probe_read_kernel_str(p->filename, ARRAY_SIZE(p->filename),
                            binprm->filename);
#endif

  // Read various fields from the task_struct in a relocatable way.
  p->spawn_namespace.pid_ns =
      BPF_CORE_READ(task, nsproxy, pid_ns_for_children, ns.inum);
  p->spawn_namespace.mnt_ns = BPF_CORE_READ(task, nsproxy, mnt_ns, ns.inum);
  p->spawn_namespace.cgroup_ns =
      BPF_CORE_READ(task, nsproxy, cgroup_ns, ns.inum);
  p->spawn_namespace.ipc_ns = BPF_CORE_READ(task, nsproxy, ipc_ns, ns.inum);
  p->spawn_namespace.net_ns = BPF_CORE_READ(task, nsproxy, net_ns, ns.inum);
  p->spawn_namespace.user_ns =
      BPF_CORE_READ(task, nsproxy, uts_ns, user_ns, ns.inum);
  p->spawn_namespace.uts_ns = BPF_CORE_READ(task, nsproxy, uts_ns, ns.inum);
  p->ppid = BPF_CORE_READ(task, real_parent, tgid);
  p->start_time = BPF_CORE_READ(task, start_boottime);
  p->parent_start_time = BPF_CORE_READ(task, real_parent, start_boottime);

  // In this case pid == tgid since this is process creation.
  p->pid = bpf_get_current_pid_tgid() >> 32;

  // GID is stored in the upper 32-bits.
  // UID is stored in the lower 32-bits.
  u64 uid_gid = bpf_get_current_uid_gid();
  p->gid = uid_gid >> 32;
  p->uid = uid_gid & 0xFFFFFFFF;

  // Submit the event to the ring buffer for userspace processing.
  bpf_ringbuf_submit(event, 0);
  return 0;
}
