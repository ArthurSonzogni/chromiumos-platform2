// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_BALLOON_H_
#define VM_TOOLS_CONCIERGE_MM_BALLOON_H_

#include <string>

#include <base/task/sequenced_task_runner.h>

namespace vm_tools::concierge::mm {

// The Balloon class represents an individual balloon device that belongs to a
// specific VM. The Balloon can be resized through DoResize(). Additionally,
// after being inflated a Balloon tracks its inflation rate to detect if the
// inflation is stalled (indicating the guest VM is very close to OOM). Upon
// detecting a stall, the Balloon will automatically slightly deflate itself and
// run the specified stall callback. All Balloon instances share a thread that
// is used for running blocking operations (such as getting and setting the
// Balloon size through crosvm_control).
class Balloon {
 public:
  Balloon(
      int vm_cid,
      const std::string& control_socket,
      scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner);
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_BALLOON_H_
