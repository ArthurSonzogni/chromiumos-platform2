// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/daemon.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/threading/thread_task_runner_handle.h>
#include <chromeos/dbus/service_constants.h>
#include <google-lpa/lpa/core/lpa.h>

#include "hermes/context.h"
#if USE_QRTR
#include "hermes/modem_qrtr.h"
#include "hermes/socket_qrtr.h"
#else
#include "hermes/modem_mbim.h"
#endif

namespace hermes {
Daemon::Daemon()
    : DBusServiceDaemon(kHermesServiceName),
      executor_(base::ThreadTaskRunnerHandle::Get()),
      smdp_(&logger_, &executor_),
      glib_bridge_(std::make_unique<glib_bridge::GlibBridge>()) {
  glib_bridge::ForwardLogs();
}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
#if USE_QRTR
  modem_ =
      ModemQrtr::Create(std::make_unique<SocketQrtr>(), &logger_, &executor_);
#else
  modem_ = ModemMbim::Create(&logger_, &executor_);
#endif

  lpa::core::Lpa::Builder b;
  b.SetEuiccCard(modem_.get())
      .SetSmdpClientFactory(&smdp_)
      .SetSmdsClientFactory(&smds_)
      .SetLogger(&logger_)
      .SetLogger(&logger_)
      .SetAutoSendNotifications(false);
  lpa_ = b.Build();

  Context::Initialize(bus_, lpa_.get(), &executor_, &adaptor_factory_,
                      modem_.get());
  manager_ = std::make_unique<Manager>();
  auto cb = base::BindOnce([](int err) {
    if (err) {
      LOG(INFO) << "Could not find eSIM:" << err;
      exit(EXIT_SUCCESS);
    }
    LOG(INFO) << "eSIM init finished";
  });

  modem_->Initialize(manager_.get(), std::move(cb));
}

}  // namespace hermes
