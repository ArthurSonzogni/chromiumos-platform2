// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_BPF_UTILS_H_
#define SECAGENTD_BPF_UTILS_H_

#include <unistd.h>
namespace secagentd {
// Used by BPF skeleton wrappers to help call a C++ class method from a C style
// callback. The void* ctx shall always point to a
// RepeatingCallback<void(const bpf::event&)>. void* data is cast into a
// bpf::event and then passed into this RepeatingCallback.
extern "C" int indirect_c_callback(void* ctx, void* data, size_t size);
}  // namespace secagentd

#endif  // SECAGENTD_BPF_UTILS_H_
