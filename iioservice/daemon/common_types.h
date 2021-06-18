// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_COMMON_TYPES_H_
#define IIOSERVICE_DAEMON_COMMON_TYPES_H_

#include <map>
#include <set>

#include <mojo/public/cpp/bindings/receiver_set.h>

#include <libmems/iio_device.h>

#include "iioservice/mojo/sensor.mojom.h"

namespace iioservice {

class ClientData {
 public:
  ClientData(const mojo::ReceiverId id,
             libmems::IioDevice* const iio_device,
             const std::set<cros::mojom::DeviceType>& types);

  bool IsActive() const;

  const mojo::ReceiverId id;
  libmems::IioDevice* const iio_device;
  const std::set<cros::mojom::DeviceType> types;

  std::set<int32_t> enabled_chn_indices;
  double frequency = -1;    // Hz
  uint32_t timeout = 5000;  // millisecond
  mojo::Remote<cros::mojom::SensorDeviceSamplesObserver> observer;
};

struct SampleData {
  // The starting index of the next sample.
  uint64_t sample_index = 0;
  // Moving averages of channels except for channels that have no batch mode
  std::map<int32_t, int64_t> chns;
};

constexpr char kInputAttr[] = "input";

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_COMMON_TYPES_H_
