// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>
#include "secagentd/bpf/process.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/bpf_utils.h"

namespace secagentd {

extern "C" int indirect_c_callback(void* ctx, void* data, size_t size) {
  if (ctx == nullptr || size < sizeof(bpf::cros_event)) {
    return -1;
  }
  auto* f = static_cast<BpfEventCb*>(ctx);
  f->Run(*static_cast<bpf::cros_event*>(data));
  return 0;
}

}  // namespace secagentd
