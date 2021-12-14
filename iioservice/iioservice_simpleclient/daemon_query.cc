// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/daemon_query.h"

#include <utility>

#include <base/bind.h>

#include "iioservice/iioservice_simpleclient/query_impl.h"

namespace iioservice {

DaemonQuery::DaemonQuery(cros::mojom::DeviceType device_type,
                         std::vector<std::string> attributes)
    : Daemon(),
      device_type_(device_type),
      attributes_(std::move(attributes)),
      weak_ptr_factory_(this) {}

DaemonQuery::~DaemonQuery() = default;

void DaemonQuery::SetSensorClient() {
  sensor_client_ = QueryImpl::Create(
      base::ThreadTaskRunnerHandle::Get(), device_type_, attributes_,
      base::BindOnce(&DaemonQuery::OnMojoDisconnect,
                     weak_ptr_factory_.GetWeakPtr()));
}

}  // namespace iioservice
