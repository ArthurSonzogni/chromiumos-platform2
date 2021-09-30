// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/common_types.h"

#include <utility>

#include <base/strings/stringprintf.h>
#include <libmems/common_types.h>

namespace iioservice {

DeviceData::DeviceData(libmems::IioDevice* const iio_device,
                       std::set<cros::mojom::DeviceType> types)
    : iio_device(iio_device), types(std::move(types)) {}

ClientData::ClientData(const mojo::ReceiverId id, DeviceData* device_data)
    : id(id), device_data(device_data) {}

bool ClientData::IsActive() const {
  return frequency >= libmems::kFrequencyEpsilon &&
         !enabled_chn_indices.empty();
}

std::vector<std::string> GetGravityChannels() {
  std::vector<std::string> channel_ids;
  for (char axis : kChannelAxes) {
    channel_ids.push_back(
        base::StringPrintf(kChannelFormat, cros::mojom::kGravityChannel, axis));
  }
  channel_ids.push_back(cros::mojom::kTimestampChannel);

  return channel_ids;
}

std::string GetSamplingFrequencyAvailable(double min_frequency,
                                          double max_frequency) {
  return base::StringPrintf(kSamplingFrequencyAvailableFormat, min_frequency,
                            max_frequency);
}

}  // namespace iioservice
