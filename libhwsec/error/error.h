// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_ERROR_ERROR_H_
#define LIBHWSEC_ERROR_ERROR_H_

#include <libhwsec-foundation/error/error.h>
#include <libhwsec-foundation/status/status_chain.h>

namespace hwsec {

using ::hwsec_foundation::status::DefaultMakeStatus;
using ::hwsec_foundation::status::Error;
using ::hwsec_foundation::status::ForbidMakeStatus;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::NewStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

}  // namespace hwsec

#endif  // LIBHWSEC_ERROR_ERROR_H_
