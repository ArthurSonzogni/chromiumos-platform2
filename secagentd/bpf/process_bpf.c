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
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf/bpf_utils.h"

const char LICENSE[] SEC("license") = "Dual BSD/GPL";
struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 65536);  // up to 2^16 task info for processes can be
  // stored.
  __type(key, uint32_t);
  __type(value, struct cros_process_start);
  __uint(map_flags, BPF_F_NO_PREALLOC);
} shared_process_info SEC(".maps");

/* BPF Verifier only allows a stack of 512 bytes max.
 * Use this one simple trick that BPF verifiers hate
 * to get around this limitation.
 */
struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
  __uint(key_size, sizeof(uint32_t));
  __uint(value_size, sizeof(struct cros_process_start));
  __uint(max_entries, 1);
} heap_shared_process_info SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, CROS_MAX_STRUCT_SIZE * 1024);
} rb SEC(".maps");

static inline __attribute__((always_inline)) void fill_ns_info(
    struct cros_namespace_info* ns_info, const struct task_struct* t) {
  ns_info->pid_ns = BPF_CORE_READ(t, nsproxy, pid_ns_for_children, ns.inum);
  ns_info->mnt_ns = BPF_CORE_READ(t, nsproxy, mnt_ns, ns.inum);
  ns_info->cgroup_ns = BPF_CORE_READ(t, nsproxy, cgroup_ns, ns.inum);
  ns_info->ipc_ns = BPF_CORE_READ(t, nsproxy, ipc_ns, ns.inum);
  ns_info->net_ns = BPF_CORE_READ(t, nsproxy, net_ns, ns.inum);
  ns_info->user_ns = BPF_CORE_READ(t, nsproxy, uts_ns, user_ns, ns.inum);
  ns_info->uts_ns = BPF_CORE_READ(t, nsproxy, uts_ns, ns.inum);
}

static inline __attribute__((always_inline)) const struct task_struct*
normalize_to_last_newns(const struct task_struct* t) {
  const struct task_struct* ret = t;
  // Arbitrarily selected limit to convince the verifier that the BPF will
  // always halt.
  for (int i = 0; i < 64; ++i) {
    struct task_struct* parent = BPF_CORE_READ(ret, real_parent, group_leader);
    if ((!parent) || (BPF_CORE_READ(parent, tgid) == 0) ||
        (BPF_CORE_READ(ret, nsproxy, mnt_ns, ns.inum) !=
         BPF_CORE_READ(parent, nsproxy, mnt_ns, ns.inum))) {
      break;
    }
    ret = parent;
  }
  return ret;
}

static inline __attribute__((always_inline)) void fill_image_info(
    struct cros_image_info* image_info,
    const struct linux_binprm* bprm,
    const struct task_struct* t) {
  // Fill in information from bprm's file inode.
  image_info->inode = BPF_CORE_READ(bprm, file, f_inode, i_ino);
  image_info->uid = BPF_CORE_READ(bprm, file, f_inode, i_uid.val);
  image_info->gid = BPF_CORE_READ(bprm, file, f_inode, i_gid.val);
  image_info->mode = BPF_CORE_READ(bprm, file, f_inode, i_mode);
  // Starting with Linux kernel v6.7-rc1, commit 12cd4402365 ("fs: rename
  // inode i_atime and i_mtime fields"), the field name changed.
  //
  // We can remove the guard after all ChromeOS kernels use __i_mtime.
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0)
  image_info->mtime.tv_sec =
      BPF_CORE_READ(bprm, file, f_inode, __i_mtime.tv_sec);
  image_info->mtime.tv_nsec =
      BPF_CORE_READ(bprm, file, f_inode, __i_mtime.tv_nsec);
#else
  image_info->mtime.tv_sec = BPF_CORE_READ(bprm, file, f_inode, i_mtime.tv_sec);
  image_info->mtime.tv_nsec =
      BPF_CORE_READ(bprm, file, f_inode, i_mtime.tv_nsec);
#endif
  // Starts from Linux kernel v6.6-rc1, commit 13bc24457850 ("fs: rename
  // i_ctime field to __i_ctime"), the field name changed.
  //
  // We can remove the guard after all ChromeOS kernels use __i_ctime.
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
  image_info->ctime.tv_sec =
      BPF_CORE_READ(bprm, file, f_inode, __i_ctime.tv_sec);
  image_info->ctime.tv_nsec =
      BPF_CORE_READ(bprm, file, f_inode, __i_ctime.tv_nsec);
#else
  image_info->ctime.tv_sec = BPF_CORE_READ(bprm, file, f_inode, i_ctime.tv_sec);
  image_info->ctime.tv_nsec =
      BPF_CORE_READ(bprm, file, f_inode, i_ctime.tv_nsec);
#endif
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

  // Fill in the mnt_ns from the task.
  image_info->mnt_ns = BPF_CORE_READ(t, nsproxy, mnt_ns, ns.inum);
  // Find an ancestral pid with the same mnt_ns. To increase the chances of its
  // ns/mnt being available to userspace.
  const struct task_struct* n = normalize_to_last_newns(t);
  image_info->pid_for_setns = BPF_CORE_READ(n, tgid);
}

// trace_sched_process_exec is called by exec_binprm shortly after exec. It has
// the distinct advantage (over arguably more stable and security focused
// interfaces like bprm_committed_creds) of running in the context of the newly
// created Task. This makes it much easier for us to grab information about this
// new Task.
SEC("tp_btf/sched_process_exec")
int BPF_PROG(handle_sched_process_exec,
             struct task_struct* current,
             pid_t old_pid,
             struct linux_binprm* bprm) {
  if (is_kthread(current)) {
    return 0;
  }
  // Reserve sample from BPF ringbuf.
  struct cros_event* event =
      (struct cros_event*)(bpf_ringbuf_reserve(&rb, sizeof(*event), 0));
  if (event == NULL) {
    return 0;
  }
  event->type = kProcessEvent;
  event->data.process_event.type = kProcessStartEvent;
  struct cros_process_start* p =
      &(event->data.process_event.data.process_start);

  cros_fill_task_info(&p->task_info, current);
  fill_ns_info(&p->spawn_namespace, current);
  fill_image_info(&p->image_info, bprm, current);
  uint32_t pid = p->task_info.pid;

  struct cros_process_start* process_start_copy;
  const uint32_t zero = 0;
  process_start_copy = bpf_map_lookup_elem(&heap_shared_process_info, &zero);
  if (process_start_copy != NULL) {
    cros_fill_task_info(&process_start_copy->task_info, current);
    fill_ns_info(&process_start_copy->spawn_namespace, current);
    fill_image_info(&process_start_copy->image_info, bprm, current);
    bpf_map_update_elem(&shared_process_info, &pid, process_start_copy,
                        BPF_NOEXIST);
  }
  // Submit the event to the ring buffer for userspace processing.
  bpf_ringbuf_submit(event, 0);
  return 0;
}

SEC("tp_btf/sched_process_exit")
int BPF_PROG(handle_sched_process_exit, struct task_struct* current) {
  if (is_kthread(current)) {
    return 0;
  }

  if ((BPF_CORE_READ(current, pid) != BPF_CORE_READ(current, tgid)) ||
      (current != cros_normalize_to_last_exec(current))) {
    // We didn't report an exec event for this task since it's either not a
    // thread group leader or it's a !CLONE_THREAD clone that hasn't exec'd
    // anything. So avoid reporting a terminate event for it.
    return 0;
  }
  struct cros_event* event =
      (struct cros_event*)(bpf_ringbuf_reserve(&rb, sizeof(*event), 0));
  if (event == NULL) {
    return 0;
  }
  event->type = kProcessEvent;
  event->data.process_event.type = kProcessExitEvent;
  struct cros_process_exit* p = &(event->data.process_event.data.process_exit);
  int pid = BPF_CORE_READ(current, tgid);
  struct cros_process_start* saved_process_start =
      bpf_map_lookup_elem(&shared_process_info, &pid);
  if (saved_process_start != NULL) {
    __builtin_memcpy(&p->task_info, &saved_process_start->task_info,
                     sizeof(p->task_info));
    __builtin_memcpy(&p->image_info, &saved_process_start->image_info,
                     sizeof(p->image_info));
    p->has_full_info = true;
    bpf_map_delete_elem(&shared_process_info, &pid);
  } else {
    // only happens when a process started before this BPF is attached
    // terminates.
    p->has_full_info = false;
    cros_fill_task_info(&p->task_info, current);
  }

  // Similar to list_empty(&current->children). Though unsure how to get a
  // reliable pointer to current->children. So instead of:
  // (&current->children == current->children.next)
  // we check if:
  // (current->children.next == current->children.next->next).
  // The only way current->children.next would link to itself is if
  // current->children.next were list head. The list head linking to itself
  // implies that the list is empty.
  struct list_head* first_child = BPF_CORE_READ(current, children.next);
  p->is_leaf =
      (!first_child || (first_child == BPF_CORE_READ(first_child, next)));

  bpf_ringbuf_submit(event, 0);
  return 0;
}
