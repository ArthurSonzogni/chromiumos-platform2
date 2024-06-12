// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_SNOOPS_INCLUDE_FDSNOOP_H_
#define BPF_SNOOPS_INCLUDE_FDSNOOP_H_

#define FDSNOOP_MAX_USTACK_ENTS 10
#define FDSNOOP_TASK_COMM_SZ 16

enum fdsnoop_event_type {
  FDSNOOP_EVENT_INVALID = 0,
  FDSNOOP_EVENT_OPEN,
  FDSNOOP_EVENT_DUP,
  FDSNOOP_EVENT_CLOSE,
};

struct fdsnoop_event {
  __u32 type;
  __s32 nfd;
  __s32 ofd;
  __u32 pid;
  __u32 tid;
  __s8 comm[FDSNOOP_TASK_COMM_SZ];
  __s16 num_ustack_ents;
  __u64 ustack_ents[FDSNOOP_MAX_USTACK_ENTS];
};

#endif  // BPF_SNOOPS_INCLUDE_FDSNOOP_H_
