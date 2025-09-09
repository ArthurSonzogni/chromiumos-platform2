// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include vmlinux.h first to declare all kernel types.
#include "include/mons/vmlinux/vmlinux.h"
// Do not move vmlinux.h include
#include <linux/errno.h>

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* This include must be after vmlinux.h */
#include "include/genmon.h"

/*
 * NOTE:
 * A workaround for v5.4, which is missing a BPF ring-buffer.
 * Once v5.4 is no longer relevant this should be replace with
 * BPF_MAP_TYPE_RINGBUF.
 */
struct {
  __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
  __uint(key_size, sizeof(int));
  __uint(value_size, sizeof(int));
} pb SEC(".maps");

/* NOTE:
 * Yet another v5.4 workaround */
#ifndef BPF_F_INDEX_MASK
#define BPF_F_INDEX_MASK 0xffffffffULL
#endif

const volatile pid_t kprobe_mon_pid = 0;

static int save_kstack(struct pt_regs* ctx, struct genmon_event* event) {
  long ret =
      bpf_get_stack(ctx, event->kstack_ents, sizeof(event->kstack_ents), 0);

  if (ret < 0) {
    return -EINVAL;
  }

  event->num_kstack_ents = ret / sizeof(event->kstack_ents[0]);
  return 0;
}

static int genmon_event(struct pt_regs* ctx) {
  struct genmon_event event = {
      0,
  };
  u64 id;

  id = bpf_get_current_pid_tgid();

  if (kprobe_mon_pid != -1 && (s32)id != kprobe_mon_pid) {
    return 0;
  }

  if (save_kstack(ctx, &event)) {
    return -EINVAL;
  }

  event.pid = (s32)id;
  event.tgid = id >> 32;
  event.ts = bpf_ktime_get_ns();
  bpf_get_current_comm(&event.comm, sizeof(event.comm));
  bpf_perf_event_output(ctx, &pb, BPF_F_INDEX_MASK, &event, sizeof(event));
  return 0;
}

/*
 * Workaround
 *
 * These macros were introduced in libbpf-1.2 while we are still
 * on libbpf-1.1
 */
#ifndef BPF_UPROBE
#define BPF_UPROBE(name, args...) BPF_KPROBE(name, ##args)
#endif

#ifndef BPF_URETPROBE
#define BPF_URETPROBE(name, args...) BPF_KRETPROBE(name, ##args)
#endif

#define KPROBE_FN(name)         \
  SEC("kprobe/" #name)          \
  int BPF_KPROBE(call_##name) { \
    return genmon_event(ctx);   \
  }

KPROBE_FN(genmon_event0);
KPROBE_FN(genmon_event1);
KPROBE_FN(genmon_event2);
KPROBE_FN(genmon_event3);
KPROBE_FN(genmon_event4);

char LICENSE[] SEC("license") = "Dual BSD/GPL";
