// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libmems/iio_context_factory.h"

#include <memory>

#include "libmems/iio_context_impl.h"

namespace libmems {

IioContextFactory::IioContextFactory() {}

std::unique_ptr<IioContext> IioContextFactory::Generate() {
  return std::make_unique<IioContextImpl>();
}

}  // namespace libmems
