// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_MONS_INCLUDE_LOCKMON_H_
#define BPF_MONS_INCLUDE_LOCKMON_H_

// Definitions for uint32_t and friends come either from vmlinux.h if
// compiled for kernel BPF or from stdint.h if compiled for user-space
// loader

#define LOCKMON_MAX_USTACK_ENTS 10
#define LOCKMON_TASK_COMM_SZ 16

enum lockmon_event_type {
  LOCKMON_EVENT_INVALID = 0,
  LOCKMON_EVENT_MUTEX_INIT,
  LOCKMON_EVENT_MUTEX_LOCK,
  LOCKMON_EVENT_MUTEX_TRYLOCK_CALL,
  LOCKMON_EVENT_MUTEX_TRYLOCK_RET,
  LOCKMON_EVENT_MUTEX_UNLOCK,
  LOCKMON_EVENT_MUTEX_DESTROY,
};

struct lockmon_event {
  int32_t pid;
  int32_t tid;
  int8_t comm[LOCKMON_TASK_COMM_SZ];
  uintptr_t lock;
  uintptr_t ustack_ents[LOCKMON_MAX_USTACK_ENTS];
  uint16_t num_ustack_ents;
  uint16_t type;
};

#endif  // BPF_MONS_INCLUDE_LOCKMON_H_
