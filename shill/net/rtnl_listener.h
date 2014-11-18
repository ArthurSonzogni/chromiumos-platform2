// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NET_RTNL_LISTENER_H_
#define SHILL_NET_RTNL_LISTENER_H_

#include <base/callback.h>

#include "shill/shill_export.h"

namespace shill {

class RTNLMessage;

class SHILL_EXPORT RTNLListener {
 public:
  RTNLListener(int listen_flags,
               const base::Callback<void(const RTNLMessage &)> &callback);
  ~RTNLListener();

  void NotifyEvent(int type, const RTNLMessage &msg);

 private:
  int listen_flags_;
  base::Callback<void(const RTNLMessage &)> callback_;

  DISALLOW_COPY_AND_ASSIGN(RTNLListener);
};

}  // namespace shill

#endif  // SHILL_NET_RTNL_LISTENER_H_
