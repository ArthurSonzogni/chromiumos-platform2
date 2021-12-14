// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_IIOSERVICE_SIMPLECLIENT_DAEMON_OBSERVER_H_
#define IIOSERVICE_IIOSERVICE_SIMPLECLIENT_DAEMON_OBSERVER_H_

#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>

#include "iioservice/iioservice_simpleclient/daemon.h"

namespace iioservice {

class DaemonObserver : public Daemon {
 public:
  DaemonObserver(int device_id,
                 cros::mojom::DeviceType device_type,
                 std::vector<std::string> channel_ids,
                 double frequency,
                 int timeout,
                 int samples);
  ~DaemonObserver() override;

 protected:
  // Daemon overrides:
  void SetSensorClient() override;

  int device_id_;
  cros::mojom::DeviceType device_type_;
  std::vector<std::string> channel_ids_;
  double frequency_;
  int timeout_;
  int samples_;

  // Must be last class member.
  base::WeakPtrFactory<DaemonObserver> weak_ptr_factory_;
};

}  // namespace iioservice

#endif  // IIOSERVICE_IIOSERVICE_SIMPLECLIENT_DAEMON_OBSERVER_H_
