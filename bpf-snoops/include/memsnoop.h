// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_SNOOPS_INCLUDE_MEMSNOOP_H_
#define BPF_SNOOPS_INCLUDE_MEMSNOOP_H_

// Definitions for uint32_t and friends come either from vmlinux.h if
// compiled for kernel BPF or from stdint.h if compiled for user-space
// loader

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
  uint32_t pid;
  unsigned int tid;
  int8_t comm[MEMSNOOP_TASK_COMM_SZ];
  uint64_t size;
  uintptr_t ptr;
  uintptr_t ustack_ents[MEMSNOOP_MAX_USTACK_ENTS];
  uint16_t num_ustack_ents;
  uint16_t type;
};

#endif  // BPF_SNOOPS_INCLUDE_MEMSNOOP_H_
