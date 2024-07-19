// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include vmlinux.h first to declare all kernel types.
#include "include/snoops/vmlinux/vmlinux.h"
// Do not move vmlinux.h include
#include <linux/errno.h>

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* This include must be after vmlinux.h */
#include "include/fdsnoop.h"

struct hkey {
  u64 call_id;
};

struct hval {
  s32 fd;
};

/*
 * We need to merge CALL and RET events for certain functions so that
 * we can record CALL arguments and returned value.
 */
struct {
  __uint(type, BPF_MAP_TYPE_LRU_HASH);
  __uint(max_entries, 1373);
  __type(key, struct hkey);
  __type(value, struct hval);
} events SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 1024 * sizeof(struct fdsnoop_event));
} rb SEC(".maps");

static u64 generate_call_id(enum fdsnoop_event_type type) {
  return (u64)((s32)type << 31) | (u32)bpf_get_current_pid_tgid();
}

static int save_ustack(struct pt_regs* ctx, struct fdsnoop_event* event) {
  long ret = bpf_get_stack(ctx, event->ustack_ents, sizeof(event->ustack_ents),
                           BPF_F_USER_STACK);

  if (ret < 0)
    return -EINVAL;

  event->num_ustack_ents = ret / sizeof(event->ustack_ents[0]);
  return 0;
}

static struct fdsnoop_event* bpf_ringbuf_event_get(void) {
  struct fdsnoop_event* event;

  event = bpf_ringbuf_reserve(&rb, sizeof(*event), 0);
  if (!event)
    return NULL;

  event->type = FDSNOOP_EVENT_INVALID;
  event->num_ustack_ents = 0;
  return event;
}

static int fdsnoop_event(struct pt_regs* ctx,
                         enum fdsnoop_event_type type,
                         s32 nfd,
                         s32 ofd) {
  struct fdsnoop_event* event;
  u64 id;

  event = bpf_ringbuf_event_get();
  if (!event)
    return -ENOMEM;

  id = bpf_get_current_pid_tgid();
  event->pid = id >> 32;
  event->tid = (u32)id;
  bpf_get_current_comm(&event->comm, sizeof(event->comm));

  if (type == FDSNOOP_EVENT_OPEN || type == FDSNOOP_EVENT_DUP) {
    if (save_ustack(ctx, event)) {
      bpf_ringbuf_submit(event, 0);
      return -EINVAL;
    }
  }

  event->type = type;
  event->nfd = nfd;
  event->ofd = ofd;

  bpf_ringbuf_submit(event, 0);
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

SEC("uretprobe")
int BPF_UPROBE(ret_open) {
  return fdsnoop_event(ctx, FDSNOOP_EVENT_OPEN, PT_REGS_RC(ctx), 0);
}

SEC("uprobe")
int BPF_UPROBE(call_dup, s32 fd) {
  struct hval v;
  struct hkey k;
  long ret;

  k.call_id = generate_call_id(FDSNOOP_EVENT_DUP);
  v.fd = fd;

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

SEC("uretprobe")
int BPF_URETPROBE(ret_dup) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(FDSNOOP_EVENT_DUP);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return fdsnoop_event(ctx, FDSNOOP_EVENT_DUP, PT_REGS_RC(ctx), v->fd);
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for old-fd to indicate
   * this
   */
  return fdsnoop_event(ctx, FDSNOOP_EVENT_DUP, PT_REGS_RC(ctx), -1);
}

SEC("uprobe")
int BPF_UPROBE(call_dup2, s32 ofd, s32 nfd) {
  return fdsnoop_event(ctx, FDSNOOP_EVENT_DUP, nfd, ofd);
}

SEC("uprobe")
int BPF_UPROBE(call_close, s32 fd) {
  return fdsnoop_event(ctx, FDSNOOP_EVENT_CLOSE, fd, 0);
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
