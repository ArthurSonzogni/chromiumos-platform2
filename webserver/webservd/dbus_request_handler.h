// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_WEBSERVD_DBUS_REQUEST_HANDLER_H_
#define WEBSERVER_WEBSERVD_DBUS_REQUEST_HANDLER_H_

#include <string>

#include <base/macros.h>
#include <dbus/object_path.h>

#include "libwebserv/dbus-proxies.h"
#include "webserver/webservd/request_handler_interface.h"

namespace webservd {

class Server;

// A D-Bus interface for a request handler.
class DBusRequestHandler final : public RequestHandlerInterface {
 public:
  using RequestHandlerProxy = org::chromium::WebServer::RequestHandlerProxy;
  DBusRequestHandler(Server* server,
                     RequestHandlerProxy* handler_proxy);

  // Called to process an incoming HTTP request this handler is subscribed
  // to handle.
  void HandleRequest(Request* request) override;

 private:
  Server* server_{nullptr};
  RequestHandlerProxy* handler_proxy_{nullptr};

  DISALLOW_COPY_AND_ASSIGN(DBusRequestHandler);
};

}  // namespace webservd

#endif  // WEBSERVER_WEBSERVD_DBUS_REQUEST_HANDLER_H_
