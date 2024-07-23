// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_MONS_INCLUDE_FDMON_H_
#define BPF_MONS_INCLUDE_FDMON_H_

// Definitions for uint32_t and friends come either from vmlinux.h if
// compiled for kernel BPF or from stdint.h if compiled for user-space
// loader

#define FDMON_MAX_USTACK_ENTS 10
#define FDMON_TASK_COMM_SZ 16

enum fdmon_event_type {
  FDMON_EVENT_INVALID = 0,
  FDMON_EVENT_OPEN,
  FDMON_EVENT_DUP,
  FDMON_EVENT_CLOSE,
};

struct fdmon_event {
  int32_t nfd;
  int32_t ofd;
  uint32_t pid;
  uint32_t tid;
  int8_t comm[FDMON_TASK_COMM_SZ];
  uintptr_t ustack_ents[FDMON_MAX_USTACK_ENTS];
  uint16_t num_ustack_ents;
  uint16_t type;
};

#endif  // BPF_MONS_INCLUDE_FDMON_H_
