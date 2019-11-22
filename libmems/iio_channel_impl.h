// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBMEMS_IIO_CHANNEL_IMPL_H_
#define LIBMEMS_IIO_CHANNEL_IMPL_H_

#include <iio.h>

#include <memory>
#include <string>

#include "libmems/export.h"
#include "libmems/iio_channel.h"

namespace libmems {

class IioDevice;

class LIBMEMS_EXPORT IioChannelImpl : public IioChannel {
 public:
  // iio_channel objects are kept alive by the IioContextImpl.
  explicit IioChannelImpl(iio_channel* channel);
  ~IioChannelImpl() override = default;

  const char* GetId() const override;

  bool IsEnabled() const override;
  bool SetEnabled(bool en) override;

  base::Optional<std::string> ReadStringAttribute(
      const std::string& name) const override;
  base::Optional<int64_t> ReadNumberAttribute(
      const std::string& name) const override;

 private:
  iio_channel* const channel_;  // non-owned

  DISALLOW_COPY_AND_ASSIGN(IioChannelImpl);
};

}  // namespace libmems

#endif  // LIBMEMS_IIO_CHANNEL_IMPL_H_
