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
#include "include/memsnoop.h"

struct hkey {
  u64 call_id;
};

struct hval {
  size_t size;
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
  __uint(max_entries, 512 * sizeof(struct memsnoop_event));
} rb SEC(".maps");

const volatile unsigned int kprobe_snoop_pid = 0;

static u64 generate_call_id(enum memsnoop_event_type type) {
  return (u64)((s32)type << 31) | (u32)bpf_get_current_pid_tgid();
}

static int save_ustack(struct pt_regs* ctx, struct memsnoop_event* event) {
  long ret = bpf_get_stack(ctx, event->ustack_ents, sizeof(event->ustack_ents),
                           BPF_F_USER_STACK);

  if (ret < 0)
    return -EINVAL;

  event->num_ustack_ents = ret / sizeof(event->ustack_ents[0]);
  return 0;
}

static struct memsnoop_event* bpf_ringbuf_event_get(void) {
  struct memsnoop_event* event;

  event = bpf_ringbuf_reserve(&rb, sizeof(*event), 0);
  if (!event)
    return NULL;

  event->type = MEMSNOOP_EVENT_INVALID;
  event->num_ustack_ents = 0;
  return event;
}

static int memsnoop_event(struct pt_regs* ctx,
                          enum memsnoop_event_type type,
                          size_t size,
                          unsigned long ptr) {
  struct memsnoop_event* event;
  u64 id;

  event = bpf_ringbuf_event_get();
  if (!event)
    return -ENOMEM;

  id = bpf_get_current_pid_tgid();
  event->pid = id >> 32;
  event->tid = (u32)id;
  bpf_get_current_comm(&event->comm, sizeof(event->comm));

  if (type == MEMSNOOP_EVENT_MALLOC || type == MEMSNOOP_EVENT_MMAP) {
    if (save_ustack(ctx, event)) {
      bpf_ringbuf_submit(event, 0);
      return -EINVAL;
    }
  }

  event->type = type;
  event->size = size;
  event->ptr = ptr;

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

SEC("uprobe")
int BPF_UPROBE(call_malloc, size_t size) {
  struct hval v;
  struct hkey k;
  long ret;

  k.call_id = generate_call_id(MEMSNOOP_EVENT_MALLOC);
  v.size = size;

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

SEC("uretprobe")
int BPF_URETPROBE(ret_malloc) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(MEMSNOOP_EVENT_MALLOC);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return memsnoop_event(ctx, MEMSNOOP_EVENT_MALLOC, v->size, PT_REGS_RC(ctx));
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for size to indicate
   * this
   */
  return memsnoop_event(ctx, MEMSNOOP_EVENT_MALLOC, -1, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_mmap, void* addr, size_t size) {
  struct hval v;
  struct hkey k;
  long ret;

  k.call_id = generate_call_id(MEMSNOOP_EVENT_MMAP);
  v.size = size;

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

SEC("uretprobe")
int BPF_URETPROBE(ret_mmap) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(MEMSNOOP_EVENT_MMAP);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return memsnoop_event(ctx, MEMSNOOP_EVENT_MMAP, v->size, PT_REGS_RC(ctx));
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for size to indicate
   * this
   */
  return memsnoop_event(ctx, MEMSNOOP_EVENT_MMAP, -1, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_munmap, void* ptr) {
  return memsnoop_event(ctx, MEMSNOOP_EVENT_MUNMAP, 0, (unsigned long)ptr);
}

SEC("uprobe")
int BPF_UPROBE(call_free, void* ptr) {
  return memsnoop_event(ctx, MEMSNOOP_EVENT_FREE, 0, (unsigned long)ptr);
}

struct vm_area_struct;

SEC("kprobe/handle_mm_fault")
int BPF_KPROBE(call_handle_mm_fault,
               struct vm_area_struct* vma,
               unsigned long ptr) {
  u32 pid = bpf_get_current_pid_tgid() >> 32;

  if (pid != kprobe_snoop_pid)
    return 0;

  return memsnoop_event(ctx, MEMSNOOP_EVENT_PF, 0, (unsigned long)ptr);
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
