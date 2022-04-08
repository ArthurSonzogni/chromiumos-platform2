// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_SYSTEM_CONTEXT_H_
#define SECANOMALYD_SYSTEM_CONTEXT_H_

class SystemContext {
 public:
  SystemContext();
  virtual ~SystemContext() = default;

  // Update all signals.
  virtual void Refresh();

  bool IsUserLoggedIn() const { return logged_in_; }

 protected:
  void set_logged_in(bool logged_in) { logged_in_ = logged_in; }

 private:
  bool logged_in_ = false;
};

#endif  // SECANOMALYD_SYSTEM_CONTEXT_H_
