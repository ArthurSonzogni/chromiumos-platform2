// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MM1_BEARER_PROXY_INTERFACE_H_
#define SHILL_CELLULAR_MM1_BEARER_PROXY_INTERFACE_H_

#include <string>

#include "shill/callbacks.h"

namespace shill {

class Error;

namespace mm1 {

// These are the methods that an org.freedesktop.ModemManager1.Bearer
// proxy must support. The interface is provided so that it can be mocked
// in tests. All calls are made asynchronously. Call completion is signalled
// via callbacks passed to the methods.
class BearerProxyInterface {
 public:
  virtual ~BearerProxyInterface() {}

  virtual void Connect(Error* error,
                       const ResultCallback& callback,
                       int timeout) = 0;
  virtual void Disconnect(Error* error,
                          const ResultCallback& callback,
                          int timeout) = 0;
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_CELLULAR_MM1_BEARER_PROXY_INTERFACE_H_
