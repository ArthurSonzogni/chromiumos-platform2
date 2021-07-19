// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_INSTANCE_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_INSTANCE_H_

#include "runtime_probe/system/context.h"

#include <type_traits>

#include <base/no_destructor.h>

namespace runtime_probe {

class ContextInstance {
 public:
  // Returns the pointer of the context instance.
  static Context* Get();
  // Creates a context object and sets as the global instance.
  template <
      typename ContextImpl,
      typename = std::enable_if_t<std::is_base_of_v<Context, ContextImpl>>>
  static void Init() {
    static base::NoDestructor<ContextImpl> impl;
    Set(impl.get());
  }

 private:
  // Sets the context instance.
  static void Set(Context* context);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_INSTANCE_H_
