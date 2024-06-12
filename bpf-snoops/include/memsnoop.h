// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_SNOOPS_INCLUDE_MEMSNOOP_H_
#define BPF_SNOOPS_INCLUDE_MEMSNOOP_H_

#define MEMSNOOP_MAX_USTACK_ENTS 10
#define MEMSNOOP_TASK_COMM_SZ 16

enum memsnoop_event_type {
  MEMSNOOP_EVENT_INVALID = 0,
  MEMSNOOP_EVENT_MALLOC,
  MEMSNOOP_EVENT_FREE,
  MEMSNOOP_EVENT_MMAP,
  MEMSNOOP_EVENT_MUNMAP,
  MEMSNOOP_EVENT_PF,
};

struct memsnoop_event {
  __u32 type;
  __u32 pid;
  __u32 tid;
  __s8 comm[MEMSNOOP_TASK_COMM_SZ];
  __u64 size;
  __u64 ptr;
  __u64 ustack_ents[MEMSNOOP_MAX_USTACK_ENTS];
  __s16 num_ustack_ents;
};

#endif  // BPF_SNOOPS_INCLUDE_MEMSNOOP_H_
