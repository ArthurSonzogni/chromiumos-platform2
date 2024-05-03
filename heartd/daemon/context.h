// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_CONTEXT_H_
#define HEARTD_DAEMON_CONTEXT_H_

#include <memory>

#include "heartd/daemon/database.h"

namespace heartd {

// A context class for holding objects which simplifies the passing of the
// objects to other objects.
class Context {
 public:
  Context();
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  ~Context();

  Database* database() const { return database_.get(); }

 private:
  // Database.
  std::unique_ptr<Database> database_ = nullptr;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_CONTEXT_H_
