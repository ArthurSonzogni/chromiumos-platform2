// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FACTORY_FACTORY_H_
#define LIBHWSEC_FACTORY_FACTORY_H_

#include <memory>
#include <utility>

#include "libhwsec/frontend/cryptohome/frontend.h"
#include "libhwsec/hwsec_export.h"

// Factory holds the ownership of the middleware and backend.
// And generates different frontend for different usage.

namespace hwsec {

class Factory {
 public:
  virtual ~Factory() = default;
  virtual std::unique_ptr<CryptohomeFrontend> GetCryptohomeFrontend() = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_FACTORY_FACTORY_H_
