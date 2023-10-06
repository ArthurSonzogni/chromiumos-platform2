// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/balloon.h"

namespace vm_tools::concierge::mm {

Balloon::Balloon(
    int vm_cid,
    const std::string& control_socket,
    scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner) {}

}  // namespace vm_tools::concierge::mm
