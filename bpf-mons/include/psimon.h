// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_MONS_INCLUDE_PSIMON_H_
#define BPF_MONS_INCLUDE_PSIMON_H_

// Definitions for uint32_t and friends come either from vmlinux.h if
// compiled for kernel BPF or from stdint.h if compiled for user-space
// loader

#define PSIMON_MAX_KSTACK_ENTS 10
#define PSIMON_TASK_COMM_SZ 16

enum psimon_event_type {
  PSIMON_EVENT_INVALID = 0,
  PSIMON_EVENT_MEMSTALL_ENTER,
  PSIMON_EVENT_MEMSTALL_LEAVE,
};

struct psimon_event {
  int32_t pid;
  int32_t tgid;
  int8_t comm[PSIMON_TASK_COMM_SZ];
  uint64_t ts;
  uintptr_t kstack_ents[PSIMON_MAX_KSTACK_ENTS];
  uint16_t num_kstack_ents;
  uint16_t type;
};

#endif  // BPF_MONS_INCLUDE_PSIMON_H_
