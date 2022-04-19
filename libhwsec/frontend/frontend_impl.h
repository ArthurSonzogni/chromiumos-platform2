// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_FRONTEND_IMPL_H_
#define LIBHWSEC_FRONTEND_FRONTEND_IMPL_H_

#include <memory>
#include <utility>

#include "libhwsec/frontend/frontend.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/middleware/middleware.h"

namespace hwsec {

class HWSEC_EXPORT FrontendImpl : public Frontend {
 public:
  explicit FrontendImpl(Middleware middleware)
      : middleware_(std::move(middleware)) {}
  ~FrontendImpl() override = default;

 protected:
  Middleware middleware_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_FRONTEND_IMPL_H_
