// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DBUS_MOCK_SOCKETSERVICE_PROXY_H_
#define PATCHPANEL_DBUS_MOCK_SOCKETSERVICE_PROXY_H_

#include <gmock/gmock.h>

#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "dbus/object_path.h"
#include "socketservice/dbus-proxies.h"

namespace patchpanel {

class StubSocketServiceProxy
    : public org::chromium::SocketServiceProxyInterface {
 public:
  bool TagSocket(const patchpanel::TagSocketRequest& in_request,
                 const base::ScopedFD& in_socket_fd,
                 patchpanel::TagSocketResponse* out_response,
                 brillo::ErrorPtr* error,
                 int timeout_ms) override {
    return false;
  }

  void TagSocketAsync(
      const patchpanel::TagSocketRequest& in_request,
      const base::ScopedFD& in_socket_fd,
      base::OnceCallback<void(
          const patchpanel::TagSocketResponse& /*response*/)> success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  const dbus::ObjectPath& GetObjectPath() const override { return path_; }
  dbus::ObjectProxy* GetObjectProxy() const override { return nullptr; }

 private:
  dbus::ObjectPath path_;
};

class MockSocketServiceProxy : public StubSocketServiceProxy {
 public:
  MockSocketServiceProxy();
  ~MockSocketServiceProxy() override;

  MOCK_METHOD(bool,
              TagSocket,
              (const patchpanel::TagSocketRequest&,
               const base::ScopedFD&,
               patchpanel::TagSocketResponse*,
               brillo::ErrorPtr*,
               int),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_DBUS_MOCK_SOCKETSERVICE_PROXY_H_
