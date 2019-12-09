// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBMEMS_IIO_CONTEXT_IMPL_H_
#define LIBMEMS_IIO_CONTEXT_IMPL_H_

#include <iio.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "libmems/export.h"
#include "libmems/iio_context.h"

namespace libmems {

class LIBMEMS_EXPORT IioContextImpl : public IioContext {
 public:
  IioContextImpl();
  ~IioContextImpl() override = default;

  void Reload() override;
  bool SetTimeout(uint32_t timeout) override;
  IioDevice* GetDevice(const std::string& name) override;

 private:
  using ContextUniquePtr =
      std::unique_ptr<iio_context, decltype(&iio_context_destroy)>;

  iio_context* GetCurrentContext() const;

  std::vector<ContextUniquePtr> context_;
  std::map<std::string, std::unique_ptr<IioDevice>> devices_;

  DISALLOW_COPY_AND_ASSIGN(IioContextImpl);
};

}  // namespace libmems

#endif  // LIBMEMS_IIO_CONTEXT_IMPL_H_
