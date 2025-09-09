// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_MONS_INCLUDE_GENMON_H_
#define BPF_MONS_INCLUDE_GENMON_H_

// Definitions for uint32_t and friends come either from vmlinux.h if
// compiled for kernel BPF or from stdint.h if compiled for user-space
// loader

#define GENMON_MAX_KSTACK_ENTS 33
#define GENMON_TASK_COMM_SZ 16

struct genmon_event {
  int32_t pid;
  int32_t tgid;
  int8_t comm[GENMON_TASK_COMM_SZ];
  uint64_t ts;
  uintptr_t kstack_ents[GENMON_MAX_KSTACK_ENTS];
  uint16_t num_kstack_ents;
};

#endif  // BPF_MONS_INCLUDE_GENMON_H_
