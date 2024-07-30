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
#include "include/memmon.h"

#define __ALIGN(x, mask) (((x) + (mask)) & ~(mask))

_Static_assert(sizeof(unsigned long) == sizeof(size_t),
               "unexpected size_t size");

struct hkey {
  u64 call_id;
};

struct hval {
  size_t payload;
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
  __uint(max_entries, 512 * sizeof(struct memmon_event));
} rb SEC(".maps");

const volatile bool ustack_dealloc_probes = false;
const volatile pid_t kprobe_mon_pid = 0;

static u64 generate_call_id(enum memmon_event_type type) {
  return (u64)((s32)type << 31) | (s32)bpf_get_current_pid_tgid();
}

static int save_ustack(struct pt_regs* ctx, struct memmon_event* event) {
  long ret = bpf_get_stack(ctx, event->ustack_ents, sizeof(event->ustack_ents),
                           BPF_F_USER_STACK);

  if (ret < 0)
    return -EINVAL;

  event->num_ustack_ents = ret / sizeof(event->ustack_ents[0]);
  return 0;
}

static struct memmon_event* bpf_ringbuf_event_get(void) {
  struct memmon_event* event;

  event = bpf_ringbuf_reserve(&rb, sizeof(*event), 0);
  if (!event)
    return NULL;

  event->type = MEMMON_EVENT_INVALID;
  event->num_ustack_ents = 0;
  return event;
}

static int memmon_event(struct pt_regs* ctx,
                        enum memmon_event_type type,
                        size_t size,
                        unsigned long ptr) {
  struct memmon_event* event;
  bool ustack = false;
  u64 id;

  event = bpf_ringbuf_event_get();
  if (!event)
    return -ENOMEM;

  id = bpf_get_current_pid_tgid();
  event->pid = id >> 32;
  event->tid = (u32)id;
  bpf_get_current_comm(&event->comm, sizeof(event->comm));

  switch (type) {
    case MEMMON_EVENT_MALLOC:
    case MEMMON_EVENT_MMAP:
    case MEMMON_EVENT_CALLOC:
    case MEMMON_EVENT_MEMALIGN:
      ustack = true;
      break;
    case MEMMON_EVENT_FREE:
    case MEMMON_EVENT_MUNMAP:
      ustack = ustack_dealloc_probes;
      break;
    default:
      break;
  }

  if (ustack && save_ustack(ctx, event)) {
    bpf_ringbuf_submit(event, 0);
    return -EINVAL;
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

static int __call_event(struct pt_regs* ctx,
                        enum memmon_event_type type,
                        size_t payload) {
  struct hval v;
  struct hkey k;
  long ret;

  k.call_id = generate_call_id(type);
  v.payload = payload;

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

static int __ret_event(struct pt_regs* ctx,
                       enum memmon_event_type type,
                       unsigned long retval) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(type);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return memmon_event(ctx, type, v->payload, retval);
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for size to indicate
   * this
   */
  return memmon_event(ctx, type, -1, retval);
}

SEC("uprobe")
int BPF_UPROBE(call_malloc, size_t size) {
  return __call_event(ctx, MEMMON_EVENT_MALLOC, size);
}

SEC("uretprobe")
int BPF_URETPROBE(ret_malloc) {
  return __ret_event(ctx, MEMMON_EVENT_MALLOC, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_mmap, void* addr, size_t size) {
  return __call_event(ctx, MEMMON_EVENT_MMAP, size);
}

SEC("uretprobe")
int BPF_URETPROBE(ret_mmap) {
  return __ret_event(ctx, MEMMON_EVENT_MMAP, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_munmap, void* ptr) {
  return memmon_event(ctx, MEMMON_EVENT_MUNMAP, 0, (unsigned long)ptr);
}

SEC("uprobe")
int BPF_UPROBE(call_calloc, size_t nmemb, size_t size) {
  return __call_event(ctx, MEMMON_EVENT_CALLOC, nmemb * size);
}

SEC("uretprobe")
int BPF_URETPROBE(ret_calloc) {
  return __ret_event(ctx, MEMMON_EVENT_CALLOC, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_memalign, size_t align, size_t size) {
  return __call_event(ctx, MEMMON_EVENT_MEMALIGN, __ALIGN(size, align));
}

SEC("uretprobe")
int BPF_URETPROBE(ret_memalign) {
  return __ret_event(ctx, MEMMON_EVENT_MEMALIGN, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_free, void* ptr) {
  return memmon_event(ctx, MEMMON_EVENT_FREE, 0, (unsigned long)ptr);
}

struct vm_area_struct;

SEC("kprobe/handle_mm_fault")
int BPF_KPROBE(call_handle_mm_fault,
               struct vm_area_struct* vma,
               unsigned long ptr) {
  s32 pid = bpf_get_current_pid_tgid() >> 32;

  if (pid != kprobe_mon_pid)
    return 0;

  return memmon_event(ctx, MEMMON_EVENT_PF, 0, (unsigned long)ptr);
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
