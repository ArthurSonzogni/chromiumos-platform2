// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SOCKET_SERVICE_ADAPTOR_H_
#define PATCHPANEL_SOCKET_SERVICE_ADAPTOR_H_

#include <base/files/scoped_file.h>
#include <base/memory/scoped_refptr.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "socketservice/dbus_adaptors/org.chromium.socketservice.h"

namespace patchpanel {

class SocketServiceAdaptor : public org::chromium::SocketServiceInterface,
                             public org::chromium::SocketServiceAdaptor {
 public:
  explicit SocketServiceAdaptor(scoped_refptr<::dbus::Bus> bus);
  virtual ~SocketServiceAdaptor();

  SocketServiceAdaptor(const SocketServiceAdaptor&) = delete;
  SocketServiceAdaptor& operator=(const SocketServiceAdaptor&) = delete;

  // Register the D-Bus methods to the D-Bus daemon.
  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  // Implements org::chromium::SocketServiceInterface.
  TagSocketResponse TagSocket(const TagSocketRequest& in_request,
                              const base::ScopedFD& in_socket_fd) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_SOCKET_SERVICE_ADAPTOR_H_
