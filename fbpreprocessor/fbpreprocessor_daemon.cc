// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/fbpreprocessor_daemon.h"

#include <sysexits.h>

#include <memory>

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/exported_object_manager.h>
#include <fbpreprocessor-client/fbpreprocessor/dbus-constants.h>

#include "fbpreprocessor/configuration.h"
#include "fbpreprocessor/manager.h"

namespace fbpreprocessor {

FbPreprocessorDaemon::FbPreprocessorDaemon(const Configuration& config)
    : brillo::DBusServiceDaemon(kFbPreprocessorServiceName) {
  manager_ = std::make_unique<Manager>(config);
}

int FbPreprocessorDaemon::OnInit() {
  int ret = brillo::DBusServiceDaemon::OnInit();
  if (ret != EX_OK)
    return ret;
  manager_->Start(bus_.get());
  return ret;
}

}  // namespace fbpreprocessor
