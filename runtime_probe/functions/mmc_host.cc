// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/mmc_host.h"

#include <base/notreached.h>

namespace runtime_probe {

MmcHostFunction::DataType MmcHostFunction::EvalImpl() const {
  NOTIMPLEMENTED();
  return {};
}

}  // namespace runtime_probe
