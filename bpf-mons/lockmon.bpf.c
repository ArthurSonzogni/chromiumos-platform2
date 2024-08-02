// Copyright 2024 The ChromiumOS Authors
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
#include "include/lockmon.h"

struct hkey {
  u64 call_id;
};

struct hval {
  unsigned long payload;
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
  __uint(max_entries, 512 * sizeof(struct lockmon_event));
} rb SEC(".maps");

static u64 generate_call_id(enum lockmon_event_type type) {
  return (u64)((s32)type << 31) | (s32)bpf_get_current_pid_tgid();
}

static int save_ustack(struct pt_regs* ctx, struct lockmon_event* event) {
  long ret = bpf_get_stack(ctx, event->ustack_ents, sizeof(event->ustack_ents),
                           BPF_F_USER_STACK);

  if (ret < 0)
    return -EINVAL;

  event->num_ustack_ents = ret / sizeof(event->ustack_ents[0]);
  return 0;
}

static struct lockmon_event* bpf_ringbuf_event_get(void) {
  struct lockmon_event* event;

  event = bpf_ringbuf_reserve(&rb, sizeof(*event), 0);
  if (!event)
    return NULL;

  event->type = LOCKMON_EVENT_INVALID;
  event->num_ustack_ents = 0;
  return event;
}

static int lockmon_event(struct pt_regs* ctx,
                         enum lockmon_event_type type,
                         unsigned long lock) {
  struct lockmon_event* event;
  bool ustack;
  u64 id;

  event = bpf_ringbuf_event_get();
  if (!event)
    return -ENOMEM;

  id = bpf_get_current_pid_tgid();
  event->pid = id >> 32;
  event->tid = (u32)id;
  bpf_get_current_comm(&event->comm, sizeof(event->comm));

  switch (type) {
    case LOCKMON_EVENT_MUTEX_INIT:
    case LOCKMON_EVENT_MUTEX_LOCK:
    case LOCKMON_EVENT_MUTEX_TRYLOCK_CALL:
    case LOCKMON_EVENT_MUTEX_TRYLOCK_RET:
    case LOCKMON_EVENT_MUTEX_UNLOCK:
    case LOCKMON_EVENT_MUTEX_DESTROY:
      ustack = true;
      break;
    default:
      ustack = false;
      break;
  }

  if (ustack && save_ustack(ctx, event)) {
    bpf_ringbuf_submit(event, 0);
    return -EINVAL;
  }

  event->type = type;
  event->lock = lock;

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

static int __call_event(struct pt_regs* ctx,
                        enum lockmon_event_type type,
                        unsigned long payload) {
  struct hval v;
  struct hkey k;
  long ret;

  k.call_id = generate_call_id(type);
  v.payload = payload;

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

static int __ret_event(struct pt_regs* ctx, enum lockmon_event_type type) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(type);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return lockmon_event(ctx, type, v->payload);
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for lock to indicate
   * this
   */
  return lockmon_event(ctx, type, -1);
}

SEC("uprobe")
int BPF_UPROBE(call_mutex_init, void* lock) {
  return lockmon_event(ctx, LOCKMON_EVENT_MUTEX_INIT, (unsigned long)lock);
}

SEC("uprobe")
int BPF_UPROBE(call_mutex_lock, void* lock) {
  return lockmon_event(ctx, LOCKMON_EVENT_MUTEX_LOCK, (unsigned long)lock);
}

SEC("uprobe")
int BPF_UPROBE(call_mutex_trylock, void* lock) {
  int ret =
      lockmon_event(ctx, LOCKMON_EVENT_MUTEX_TRYLOCK_CALL, (unsigned long)lock);

  if (ret)
    return ret;

  ret = __call_event(ctx, LOCKMON_EVENT_MUTEX_TRYLOCK_RET, (unsigned long)lock);
  return ret;
}

SEC("uretprobe")
int BPF_URETPROBE(ret_mutex_trylock, void* lock) {
  /* Only successful try_lock() is reported */
  if (PT_REGS_RC(ctx) != 0)
    return 0;

  return __ret_event(ctx, LOCKMON_EVENT_MUTEX_TRYLOCK_RET);
}

SEC("uprobe")
int BPF_UPROBE(call_mutex_unlock, void* lock) {
  return lockmon_event(ctx, LOCKMON_EVENT_MUTEX_UNLOCK, (unsigned long)lock);
}

SEC("uprobe")
int BPF_UPROBE(call_mutex_destroy, void* lock) {
  return lockmon_event(ctx, LOCKMON_EVENT_MUTEX_DESTROY, (unsigned long)lock);
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
