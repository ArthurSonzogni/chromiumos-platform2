// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_SYSTEM_CONTEXT_H_
#define SECANOMALYD_SYSTEM_CONTEXT_H_

#include <session_manager/dbus-proxies.h>

using SessionManagerProxy = org::chromium::SessionManagerInterfaceProxy;
using SessionManagerProxyInterface =
    org::chromium::SessionManagerInterfaceProxyInterface;

class SystemContext {
 public:
  explicit SystemContext(SessionManagerProxyInterface* session_manager);
  virtual ~SystemContext() = default;

  // Update all signals.
  virtual void Refresh();

  bool IsUserLoggedIn() const { return logged_in_; }

 protected:
  SystemContext() = default;
  void set_logged_in(bool logged_in) { logged_in_ = logged_in; }

 private:
  bool UpdateLoggedInState();

  // Un-owned.
  SessionManagerProxyInterface* session_manager_;
  bool logged_in_ = false;
};

#endif  // SECANOMALYD_SYSTEM_CONTEXT_H_
