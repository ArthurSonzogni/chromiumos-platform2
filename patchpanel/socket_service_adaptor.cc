// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/socket_service_adaptor.h"

#include <utility>

#include <brillo/dbus/async_event_sequencer.h>
#include <chromeos/dbus/patchpanel/dbus-constants.h>
#include <dbus/object_path.h>

namespace patchpanel {

SocketServiceAdaptor::SocketServiceAdaptor(scoped_refptr<::dbus::Bus> bus)
    : org::chromium::SocketServiceAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kSocketServicePath)) {}

SocketServiceAdaptor::~SocketServiceAdaptor() {}

void SocketServiceAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

TagSocketResponse SocketServiceAdaptor::TagSocket(
    const TagSocketRequest& in_request, const base::ScopedFD& in_socket_fd) {
  // TODO(b/331620358): call the routing service to serve the call.
  return {};
}

}  // namespace patchpanel
