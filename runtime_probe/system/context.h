// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_H_

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
  Context() = default;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  virtual ~Context() = default;

  // Use the object returned by debugd_proxy() to make calls to debugd.
  virtual org::chromium::debugdProxyInterface* debugd_proxy() = 0;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_H_
