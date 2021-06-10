// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_daemon.h"

#include <utility>

#include <base/check.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/dlp/dbus-constants.h>

#include "dlp/dlp_adaptor.h"

namespace dlp {

namespace {
const char kObjectServicePath[] = "/org/chromium/Dlp/ObjectManager";
}  // namespace

DlpDaemon::DlpDaemon()
    : DBusServiceDaemon(kDlpServiceName, kObjectServicePath) {}
DlpDaemon::~DlpDaemon() = default;

void DlpDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  auto dbus_object = std::make_unique<brillo::dbus_utils::DBusObject>(
      object_manager_.get(), object_manager_->GetBus(),
      org::chromium::DlpAdaptor::GetObjectPath());
  DCHECK(!adaptor_);
  adaptor_ = std::make_unique<DlpAdaptor>(std::move(dbus_object));
  adaptor_->InitDatabaseOnCryptohome();
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

}  // namespace dlp
