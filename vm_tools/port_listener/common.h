// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_PORT_LISTENER_COMMON_H_
#define VM_TOOLS_PORT_LISTENER_COMMON_H_

enum State {
  kPortListenerUp,
  kPortListenerDown,
};

struct event {
  enum State state;
  uint16_t port;
};

#endif  // VM_TOOLS_PORT_LISTENER_COMMON_H_
