// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides internal implementation details of dispatching D-Bus
// method calls to a D-Bus object methods by reading the expected parameter
// values from D-Bus message buffer then invoking a native C++ callback with
// those parameters passed in. If the callback returns a value, that value is
// sent back to the caller of D-Bus method via the response message.

// This is achieved by redirecting the parsing of parameter values from D-Bus
// message buffer to DBusParamReader helper class.
// DBusParamReader de-serializes the parameter values from the D-Bus message
// and calls the provided native C++ callback with those arguments.
// However it expects the callback with a simple signature like this:
//    void callback(Args...);
// The method handlers for DBusObject, on the other hand, have one of the
// following signatures:
//    void handler(Args...);
//    ReturnType handler(Args...);
//    bool handler(ErrorPtr* error, Args...);
//    void handler(std::unique_ptr<DBusMethodResponse<T1, T2,...>>, Args...);
//
// To make this all work, we craft a simple callback suitable for
// DBusParamReader using a lambda in DBusInvoker::Invoke() and redirect the call
// to the appropriate method handler using additional data captured by the
// lambda object.

// TODO(b/289932268): Remove the file after the clean up.

#ifndef LIBBRILLO_BRILLO_DBUS_DBUS_OBJECT_INTERNAL_IMPL_H_
#define LIBBRILLO_BRILLO_DBUS_DBUS_OBJECT_INTERNAL_IMPL_H_

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <brillo/dbus/data_serialization.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/dbus_param_reader.h>
#include <brillo/dbus/dbus_param_writer.h>
#include <brillo/dbus/utils.h>
#include <brillo/errors/error.h>
#include <dbus/message.h>

namespace brillo {
namespace dbus_utils {

// This is an abstract base class to allow dispatching a native C++ callback
// method when a corresponding D-Bus method is called.
class DBusInterfaceMethodHandlerInterface {
 public:
  virtual ~DBusInterfaceMethodHandlerInterface() = default;

  // Returns true if the method has been handled synchronously (whether or not
  // a success or error response message had been sent).
  virtual void HandleMethod(::dbus::MethodCall* method_call,
                            ResponseSender sender) = 0;
};

// An implementation of DBusInterfaceMethodHandlerInterface that has custom
// processing of both input and output parameters. This class is used by
// DBusObject::AddRawMethodHandler and expects the callback to be of the
// following signature:
//    void(dbus::MethodCall*, ResponseSender)
// It will be up to the callback to parse the input parameters from the
// message buffer and construct the D-Bus Response object.
class RawDBusInterfaceMethodHandler
    : public DBusInterfaceMethodHandlerInterface {
 public:
  // A constructor that takes a |handler| to be called when HandleMethod()
  // virtual function is invoked.
  RawDBusInterfaceMethodHandler(
      const base::RepeatingCallback<void(::dbus::MethodCall*, ResponseSender)>&
          handler)
      : handler_(handler) {}
  RawDBusInterfaceMethodHandler(const RawDBusInterfaceMethodHandler&) = delete;
  RawDBusInterfaceMethodHandler& operator=(
      const RawDBusInterfaceMethodHandler&) = delete;

  void HandleMethod(::dbus::MethodCall* method_call,
                    ResponseSender sender) override {
    handler_.Run(method_call, std::move(sender));
  }

 private:
  // C++ callback to be called when a D-Bus method is dispatched.
  base::RepeatingCallback<void(::dbus::MethodCall*, ResponseSender)> handler_;
};

}  // namespace dbus_utils
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_DBUS_DBUS_OBJECT_INTERNAL_IMPL_H_
