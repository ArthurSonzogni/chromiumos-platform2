// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_H_

#include <base/files/file_path.h>

#include "runtime_probe/system/helper_invoker.h"

namespace org {
namespace chromium {
class debugdProxyInterface;
}  // namespace chromium
}  // namespace org

namespace runtime_probe {

// A context class for holding the helper objects used in runtime probe, which
// simplifies the passing of the helper objects to other objects. For instance,
// instead of passing various helper objects to an object via its constructor,
// the context object is passed.
class Context {
 public:
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  // Returns the current global context instance. The global instance will be
  // overridden by derived classes. Only one global instance is allowed at a
  // time.
  static Context* Get();

  // Use the object returned by debugd_proxy() to make calls to debugd.
  virtual org::chromium::debugdProxyInterface* debugd_proxy() = 0;

  // The object to invoke the runtime_probe helper.
  virtual HelperInvoker* helper_invoker() = 0;

  // Returns the root directory. This can be overridden during test.
  virtual const base::FilePath& root_dir();

 protected:
  Context();
  virtual ~Context();
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_H_
