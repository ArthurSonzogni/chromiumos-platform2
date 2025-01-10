// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBMEMS_IIO_CONTEXT_FACTORY_H_
#define LIBMEMS_IIO_CONTEXT_FACTORY_H_

#include <memory>

#include "libmems/export.h"
#include "libmems/iio_context.h"

namespace libmems {

class LIBMEMS_EXPORT IioContextFactory {
 public:
  IioContextFactory();
  virtual ~IioContextFactory() = default;

  virtual std::unique_ptr<IioContext> Generate();
};

}  // namespace libmems

#endif  // LIBMEMS_IIO_CONTEXT_FACTORY_H_
