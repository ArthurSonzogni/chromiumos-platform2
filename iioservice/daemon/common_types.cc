// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/common_types.h"

#include <libmems/common_types.h>

namespace iioservice {

ClientData::ClientData(const mojo::ReceiverId id,
                       libmems::IioDevice* const iio_device,
                       const std::set<cros::mojom::DeviceType>& types)
    : id(id), iio_device(iio_device), types(types) {}

bool ClientData::IsActive() const {
  return frequency >= libmems::kFrequencyEpsilon &&
         !enabled_chn_indices.empty();
}

}  // namespace iioservice
