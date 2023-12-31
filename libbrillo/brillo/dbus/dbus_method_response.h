// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_DBUS_DBUS_METHOD_RESPONSE_H_
#define LIBBRILLO_BRILLO_DBUS_DBUS_METHOD_RESPONSE_H_

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/location.h>
#include <base/logging.h>
#include <brillo/brillo_export.h>
#include <brillo/dbus/data_serialization.h>
#include <brillo/errors/error.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>

namespace brillo {

class Error;

namespace dbus_utils {

using ResponseSender = ::dbus::ExportedObject::ResponseSender;

// DBusMethodResponseBase is a helper class used with asynchronous D-Bus method
// handlers to encapsulate the information needed to send the method call
// response when it is available.
class BRILLO_EXPORT DBusMethodResponseBase {
 public:
  DBusMethodResponseBase(::dbus::MethodCall* method_call,
                         ResponseSender sender);
  DBusMethodResponseBase(DBusMethodResponseBase&& other)
      : sender_(std::exchange(
            other.sender_,
            base::BindOnce([](std::unique_ptr<dbus::Response> response) {
              LOG(DFATAL)
                  << "Empty DBusMethodResponseBase attempts to send a response";
            }))),
        method_call_(std::exchange(other.method_call_, nullptr)) {}
  DBusMethodResponseBase& operator=(DBusMethodResponseBase&& other) = delete;

  virtual ~DBusMethodResponseBase();

  // Sends an error response. Marshals the |error| object over D-Bus.
  // If |error| is from the "dbus" error domain, takes the |error_code| from
  // |error| and uses it as the DBus error name.
  // For error is from other domains, the full error information (domain, error
  // code, error message) is encoded into the D-Bus error message and returned
  // to the caller as "org.freedesktop.DBus.Failed".
  virtual void ReplyWithError(const brillo::Error* error);

  // Constructs brillo::Error object from the parameters specified and send
  // the error information over D-Bus using the method above.
  virtual void ReplyWithError(const base::Location& location,
                              const std::string& error_domain,
                              const std::string& error_code,
                              const std::string& error_message);

  // Sends a raw D-Bus response message.
  void SendRawResponse(std::unique_ptr<::dbus::Response> response);

  // Creates a custom response object for the current method call.
  std::unique_ptr<::dbus::Response> CreateCustomResponse() const;

  // Checks if the response has been sent already.
  bool IsResponseSent() const;

 protected:
  void CheckCanSendResponse() const;

  // Aborts the method execution. Does not send any response message.
  void Abort();

 private:
  // A callback to be called to send the method call response message.
  ResponseSender sender_;
  // |method_call_| is actually owned by |sender_| (it is embedded as unique_ptr
  // in the bound parameter list in the Callback). We set it to nullptr after
  // the method call response has been sent to ensure we can't possibly try
  // to send a response again somehow.
  ::dbus::MethodCall* method_call_;
};

// DBusMethodResponse is an explicitly-typed version of DBusMethodResponse.
// Using DBusMethodResponse<Types...> indicates the types a D-Bus method
// is expected to return.
template <typename... Types>
class DBusMethodResponse : public DBusMethodResponseBase {
 public:
  // Make the base class's custom constructor available to DBusMethodResponse.
  using DBusMethodResponseBase::DBusMethodResponseBase;

  // Sends the a successful response. |return_values| can contain a list
  // of return values to be sent to the caller.
  virtual void Return(const Types&... return_values) {
    CheckCanSendResponse();
    auto response = CreateCustomResponse();
    ::dbus::MessageWriter writer(response.get());
    WriteDBusArgs(&writer, return_values...);
    SendRawResponse(std::move(response));
  }
};

}  // namespace dbus_utils
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_DBUS_DBUS_METHOD_RESPONSE_H_
