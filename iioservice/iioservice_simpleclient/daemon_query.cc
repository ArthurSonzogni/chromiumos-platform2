// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/daemon_query.h"

#include <sysexits.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <mojo/core/embedder/embedder.h>

#include "iioservice/iioservice_simpleclient/query_impl.h"
#include "iioservice/include/common.h"

namespace iioservice {

DaemonQuery::DaemonQuery(cros::mojom::DeviceType device_type,
                         std::vector<std::string> attributes)
    : device_type_(device_type),
      attributes_(std::move(attributes)),
      weak_ptr_factory_(this) {}

DaemonQuery::~DaemonQuery() {}

int DaemonQuery::OnInit() {
  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);

  SetBus(bus_.get());
  BootstrapMojoConnection();

  query_ = QueryImpl::Create(base::ThreadTaskRunnerHandle::Get(), device_type_,
                             attributes_,
                             base::BindOnce(&DaemonQuery::OnMojoDisconnect,
                                            weak_ptr_factory_.GetWeakPtr()));

  return exit_code;
}

void DaemonQuery::OnClientReceived(
    mojo::PendingReceiver<cros::mojom::SensorHalClient> client) {
  query_->BindClient(std::move(client));
}

void DaemonQuery::OnMojoDisconnect() {
  LOGF(INFO) << "Quitting this process.";
  Quit();
}

}  // namespace iioservice
