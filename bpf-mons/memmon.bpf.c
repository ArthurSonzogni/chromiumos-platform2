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

struct hkey {
  u64 call_id;
};

struct hval {
  union {
    size_t size;
    unsigned long ptr;
  };
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

const volatile unsigned int kprobe_mon_pid = 0;

static u64 generate_call_id(enum memmon_event_type type) {
  return (u64)((s32)type << 31) | (u32)bpf_get_current_pid_tgid();
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
    case MEMMON_EVENT_STRDUP:
    case MEMMON_EVENT_MEMALIGN:
      if (save_ustack(ctx, event)) {
        bpf_ringbuf_submit(event, 0);
        return -EINVAL;
      }
      break;
    default:
      break;
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

  k.call_id = generate_call_id(MEMMON_EVENT_MALLOC);
  v.size = size;

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

SEC("uretprobe")
int BPF_URETPROBE(ret_malloc) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(MEMMON_EVENT_MALLOC);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return memmon_event(ctx, MEMMON_EVENT_MALLOC, v->size, PT_REGS_RC(ctx));
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for size to indicate
   * this
   */
  return memmon_event(ctx, MEMMON_EVENT_MALLOC, -1, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_mmap, void* addr, size_t size) {
  struct hval v;
  struct hkey k;
  long ret;

  k.call_id = generate_call_id(MEMMON_EVENT_MMAP);
  v.size = size;

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

SEC("uretprobe")
int BPF_URETPROBE(ret_mmap) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(MEMMON_EVENT_MMAP);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return memmon_event(ctx, MEMMON_EVENT_MMAP, v->size, PT_REGS_RC(ctx));
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for size to indicate
   * this
   */
  return memmon_event(ctx, MEMMON_EVENT_MMAP, -1, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_munmap, void* ptr) {
  return memmon_event(ctx, MEMMON_EVENT_MUNMAP, 0, (unsigned long)ptr);
}

SEC("uprobe")
int BPF_UPROBE(call_strdup, const char* p) {
  struct hval v;
  struct hkey k;
  long ret;

  k.call_id = generate_call_id(MEMMON_EVENT_STRDUP);
  v.ptr = (unsigned long)p;

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

SEC("uretprobe")
int BPF_URETPROBE(ret_strdup) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(MEMMON_EVENT_STRDUP);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return memmon_event(ctx, MEMMON_EVENT_STRDUP, v->ptr, PT_REGS_RC(ctx));
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for size to indicate
   * this
   */
  return memmon_event(ctx, MEMMON_EVENT_STRDUP, -1, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_calloc, size_t nmemb, size_t size) {
  struct hval v;
  struct hkey k;
  long ret;

  k.call_id = generate_call_id(MEMMON_EVENT_CALLOC);
  v.size = nmemb * size;

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

SEC("uretprobe")
int BPF_URETPROBE(ret_calloc) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(MEMMON_EVENT_CALLOC);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return memmon_event(ctx, MEMMON_EVENT_CALLOC, v->size, PT_REGS_RC(ctx));
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for size to indicate
   * this
   */
  return memmon_event(ctx, MEMMON_EVENT_CALLOC, -1, PT_REGS_RC(ctx));
}

SEC("uprobe")
int BPF_UPROBE(call_memalign, size_t align, size_t size) {
  struct hval v;
  struct hkey k;
  long ret;

  k.call_id = generate_call_id(MEMMON_EVENT_MEMALIGN);
  v.size = __ALIGN(size, align);

  ret = bpf_map_update_elem(&events, &k, &v, BPF_ANY);
  return !ret ? 0 : -EINVAL;
}

SEC("uretprobe")
int BPF_URETPROBE(ret_memalign) {
  struct hval* v;
  struct hkey k;

  k.call_id = generate_call_id(MEMMON_EVENT_MEMALIGN);
  v = bpf_map_lookup_elem(&events, &k);
  if (v) {
    return memmon_event(ctx, MEMMON_EVENT_MEMALIGN, v->size, PT_REGS_RC(ctx));
  }

  /*
   * We didn't find the pairing CALL event, so use -1 for size to indicate
   * this
   */
  return memmon_event(ctx, MEMMON_EVENT_MEMALIGN, -1, PT_REGS_RC(ctx));
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
  u32 pid = bpf_get_current_pid_tgid() >> 32;

  if (pid != kprobe_mon_pid)
    return 0;

  return memmon_event(ctx, MEMMON_EVENT_PF, 0, (unsigned long)ptr);
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
