// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_FRONTEND_H_
#define LIBHWSEC_FRONTEND_FRONTEND_H_

#include "libhwsec/hwsec_export.h"

namespace hwsec {

// Frontend is the layer to simplify the call to backend. And provide specific
// to different application. Note: This layer should be state-less and thread
// safe.
class HWSEC_EXPORT Frontend {
 public:
  virtual ~Frontend() = default;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_FRONTEND_H_
