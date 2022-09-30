// Copyright 2022 The ChromiumOS Authors
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

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

const char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, MAX_STRUCT_SIZE * 1024);
} rb SEC(".maps");

static inline __attribute__((always_inline)) void fill_ns_info(
    struct namespace_info* ns_info, const struct task_struct* t) {
  ns_info->pid_ns = BPF_CORE_READ(t, nsproxy, pid_ns_for_children, ns.inum);
  ns_info->mnt_ns = BPF_CORE_READ(t, nsproxy, mnt_ns, ns.inum);
  ns_info->cgroup_ns = BPF_CORE_READ(t, nsproxy, cgroup_ns, ns.inum);
  ns_info->ipc_ns = BPF_CORE_READ(t, nsproxy, ipc_ns, ns.inum);
  ns_info->net_ns = BPF_CORE_READ(t, nsproxy, net_ns, ns.inum);
  ns_info->user_ns = BPF_CORE_READ(t, nsproxy, uts_ns, user_ns, ns.inum);
  ns_info->uts_ns = BPF_CORE_READ(t, nsproxy, uts_ns, ns.inum);
}

static inline __attribute__((always_inline)) void fill_image_info(
    struct image_info* image_info,
    const struct linux_binprm* bprm,
    const struct task_struct* t) {
  // Fill in information from bprm's file inode.
  image_info->inode = BPF_CORE_READ(bprm, file, f_inode, i_ino);
  image_info->uid = BPF_CORE_READ(bprm, file, f_inode, i_uid.val);
  image_info->gid = BPF_CORE_READ(bprm, file, f_inode, i_gid.val);
  image_info->mode = BPF_CORE_READ(bprm, file, f_inode, i_mode);
  // Mimic new_encode_dev() to get stat-like dev_id.
  dev_t dev = BPF_CORE_READ(bprm, file, f_inode, i_sb, s_dev);
  unsigned major = dev >> 20;
  unsigned minor = dev & ((1 << 20) - 1);
  image_info->inode_device_id =
      (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);

  // Fill in pathname from bprm. Interp is the actual binary that executed post
  // symlink and interpreter resolution.
  const char* interp_start = BPF_CORE_READ(bprm, interp);
  bpf_probe_read_str(image_info->pathname, sizeof(image_info->pathname),
                     interp_start);

  // Fill in mnt_ns from the parent context.
  image_info->mnt_ns = BPF_CORE_READ(t, real_parent, nsproxy, mnt_ns, ns.inum);
}

// trace_sched_process_exec is called by exec_binprm shortly after exec. It has
// the distinct advantage (over arguably more stable and security focused
// interfaces like bprm_committed_creds) of running in the context of the newly
// created Task. This makes it much easier for us to grab information about this
// new Task.
#if defined(USE_MIN_CORE_BTF) && USE_MIN_CORE_BTF == 1
// tp_btf will make libbpf silently fall back to looking for a full vmlinux BTF.
// So use a raw tracepoint instead.
SEC("raw_tracepoint/sched_process_exec")
#else
SEC("tp_btf/sched_process_exec")
#endif  // USE_MIN_CORE_BTF
int BPF_PROG(handle_sched_process_exec,
             struct task_struct* current,
             pid_t old_pid,
             struct linux_binprm* bprm) {
  // Reserve sample from BPF ringbuf.
  struct event* event =
      (struct event*)(bpf_ringbuf_reserve(&rb, sizeof(*event), 0));
  if (event == NULL) {
    return 0;
  }
  event->type = process_type;
  event->data.process_event.type = process_start_type;
  struct process_start* p = &(event->data.process_event.data.process_start);

  // Read various fields from the task_struct in a relocatable way.
  fill_ns_info(&p->spawn_namespace, current);
  fill_image_info(&p->image_info, bprm, current);
  p->ppid = BPF_CORE_READ(current, real_parent, tgid);
  p->start_time = BPF_CORE_READ(current, start_boottime);
  p->parent_start_time = BPF_CORE_READ(current, real_parent, start_boottime);

  // In this case pid == tgid since this is process creation.
  p->pid = bpf_get_current_pid_tgid() >> 32;

  // GID is stored in the upper 32-bits.
  // UID is stored in the lower 32-bits.
  u64 uid_gid = bpf_get_current_uid_gid();
  p->gid = uid_gid >> 32;
  p->uid = uid_gid & 0xFFFFFFFF;

  // Read argv from user memory.
  uint64_t arg_start = BPF_CORE_READ(current, mm, arg_start);
  uint64_t arg_end = BPF_CORE_READ(current, mm, arg_end);
  p->commandline_len = MIN((arg_end - arg_start), sizeof(p->commandline));
  bpf_probe_read_user(p->commandline, p->commandline_len, arg_start);
  if (p->commandline_len == sizeof(p->commandline)) {
    p->commandline[p->commandline_len - 1] = '\0';
  }

  // Submit the event to the ring buffer for userspace processing.
  bpf_ringbuf_submit(event, 0);
  return 0;
}
