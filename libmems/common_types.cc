// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libmems/common_types.h"

namespace libmems {

uint64_t IioEventCode(iio_chan_type chan_type,
                      iio_event_type event_type,
                      iio_event_direction dir,
                      int channel) {
  return (uint64_t)chan_type << 32 | (uint64_t)dir << 48 |
         (uint64_t)event_type << 56 | (uint64_t)channel;
  // TODO(chenghaoyang): use the existing IIO_EVENT_CODE instead.
  // return IIO_EVENT_CODE(chan_type_, 0, 0, dir, event_type_, channel_, 0, 0);
}

}  // namespace libmems
