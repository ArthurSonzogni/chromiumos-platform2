// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_MONS_INCLUDE_MEMMON_H_
#define BPF_MONS_INCLUDE_MEMMON_H_

// Definitions for uint32_t and friends come either from vmlinux.h if
// compiled for kernel BPF or from stdint.h if compiled for user-space
// loader

#define MEMMON_MAX_USTACK_ENTS 10
#define MEMMON_TASK_COMM_SZ 16

enum memmon_event_type {
  MEMMON_EVENT_INVALID = 0,
  MEMMON_EVENT_MALLOC,
  MEMMON_EVENT_CALLOC,
  MEMMON_EVENT_MEMALIGN,
  MEMMON_EVENT_FREE,
  MEMMON_EVENT_MMAP,
  MEMMON_EVENT_MUNMAP,
  MEMMON_EVENT_PF,
};

struct memmon_event {
  int32_t pid;
  int32_t tid;
  int8_t comm[MEMMON_TASK_COMM_SZ];
  uint64_t size;
  uintptr_t ptr;
  uintptr_t ustack_ents[MEMMON_MAX_USTACK_ENTS];
  uint16_t num_ustack_ents;
  uint16_t type;
};

#endif  // BPF_MONS_INCLUDE_MEMMON_H_
