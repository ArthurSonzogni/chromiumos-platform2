// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_SNOOPS_INCLUDE_FDSNOOP_H_
#define BPF_SNOOPS_INCLUDE_FDSNOOP_H_

// Definitions for uint32_t and friends come either from vmlinux.h if
// compiled for kernel BPF or from stdint.h if compiled for user-space
// loader

#define FDSNOOP_MAX_USTACK_ENTS 10
#define FDSNOOP_TASK_COMM_SZ 16

enum fdsnoop_event_type {
  FDSNOOP_EVENT_INVALID = 0,
  FDSNOOP_EVENT_OPEN,
  FDSNOOP_EVENT_DUP,
  FDSNOOP_EVENT_CLOSE,
};

struct fdsnoop_event {
  int32_t nfd;
  int32_t ofd;
  uint32_t pid;
  uint32_t tid;
  int8_t comm[FDSNOOP_TASK_COMM_SZ];
  uintptr_t ustack_ents[FDSNOOP_MAX_USTACK_ENTS];
  uint16_t num_ustack_ents;
  uint16_t type;
};

#endif  // BPF_SNOOPS_INCLUDE_FDSNOOP_H_
