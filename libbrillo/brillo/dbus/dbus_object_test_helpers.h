// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper utilities to simplify testing of D-Bus object implementations.
// Since the method handlers could now be asynchronous, they use callbacks to
// provide method return values. This makes it really difficult to invoke
// such handlers in unit tests (even if they are actually synchronous but
// still use DBusMethodResponse to send back the method results).
// This file provide testing-only helpers to make calling D-Bus method handlers
// easier.
#ifndef LIBBRILLO_BRILLO_DBUS_DBUS_OBJECT_TEST_HELPERS_H_
#define LIBBRILLO_BRILLO_DBUS_DBUS_OBJECT_TEST_HELPERS_H_

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/test/bind.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_object.h>

namespace brillo {
namespace dbus_utils {

// Helper friend class to call DBusInterface::HandleMethodCall() since it is
// a private method of the class and we don't want to make it public.
class DBusInterfaceTestHelper final {
 public:
  static void HandleMethodCall(DBusInterface* itf,
                               ::dbus::MethodCall* method_call,
                               ResponseSender sender) {
    itf->HandleMethodCall(method_call, std::move(sender));
  }
};

namespace testing {

// Dispatches a D-Bus method call to the corresponding handler.
// Used for testing purposes. This method is inlined so that it is not included
// in the shipping code of libbrillo, and included at the call sites. Returns a
// response from the method handler. If the method hasn't provided the response
// message immediately (i.e. it is asynchronous), aborts with a CHECK().
inline std::unique_ptr<::dbus::Response> CallMethod(
    const DBusObject& object, ::dbus::MethodCall* method_call) {
  DBusInterface* itf = object.FindInterface(method_call->GetInterface());
  std::unique_ptr<::dbus::Response> response;
  if (!itf) {
    response = dbus::ErrorResponse::FromMethodCall(
        method_call, DBUS_ERROR_UNKNOWN_INTERFACE,
        "Interface you invoked a method on isn't known by the object.");
  } else {
    DBusInterfaceTestHelper::HandleMethodCall(
        itf, method_call,
        base::BindLambdaForTesting(
            [&response](std::unique_ptr<::dbus::Response> response_in) {
              response = std::move(response_in);
            }));
    CHECK(response)
        << "No response received. Asynchronous methods are not supported.";
  }
  return response;
}

// MethodHandlerInvoker is similar to CallMethod() function above, except
// it allows the callers to invoke the method handlers directly bypassing
// the DBusObject/DBusInterface infrastructure.
// This works only on synchronous methods though. The handler must reply
// before the handler exits.
template <typename RetType>
struct MethodHandlerInvoker {
  // MethodHandlerInvoker<RetType>::Call() calls a member |method| of a class
  // |instance| and passes the |args| to it. The method's return value provided
  // via handler's DBusMethodResponse is then extracted and returned.
  // If the method handler returns an error, the error information is passed
  // to the caller via the |error| object (and the method returns a default
  // value of type RetType as a placeholder).
  // If the method handler asynchronous and did not provide a reply (success or
  // error) before the handler exits, this method aborts with a CHECK().
  template <class Class, typename... Params, typename... Args>
  static RetType Call(
      ErrorPtr* error,
      Class* instance,
      void (Class::*method)(std::unique_ptr<DBusMethodResponse<RetType>>,
                            Params...),
      Args... args) {
    ::dbus::MethodCall method_call("test.interface", "TestMethod");
    method_call.SetSerial(123);
    std::unique_ptr<::dbus::Response> response;
    std::unique_ptr<DBusMethodResponse<RetType>> method_response{
        new DBusMethodResponse<RetType>(
            &method_call,
            base::BindLambdaForTesting(
                [&response](std::unique_ptr<::dbus::Response> response_in) {
                  response = std::move(response_in);
                }))};
    (instance->*method)(std::move(method_response), args...);
    CHECK(response)
        << "No response received. Asynchronous methods are not supported.";
    RetType ret_val;
    ExtractMethodCallResults(response.get(), error, &ret_val);
    return ret_val;
  }
};

// Specialization of MethodHandlerInvoker for methods that do not return
// values (void methods).
template <>
struct MethodHandlerInvoker<void> {
  template <class Class, typename... Params, typename... Args>
  static void Call(ErrorPtr* error,
                   Class* instance,
                   void (Class::*method)(std::unique_ptr<DBusMethodResponse<>>,
                                         Params...),
                   Args... args) {
    ::dbus::MethodCall method_call("test.interface", "TestMethod");
    method_call.SetSerial(123);
    std::unique_ptr<::dbus::Response> response;
    std::unique_ptr<DBusMethodResponse<>> method_response{
        new DBusMethodResponse<>(
            &method_call,
            base::BindLambdaForTesting(
                [&response](std::unique_ptr<::dbus::Response> response_in) {
                  response = std::move(response_in);
                }))};
    (instance->*method)(std::move(method_response), args...);
    CHECK(response)
        << "No response received. Asynchronous methods are not supported.";
    ExtractMethodCallResults(response.get(), error);
  }
};

}  // namespace testing
}  // namespace dbus_utils
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_DBUS_DBUS_OBJECT_TEST_HELPERS_H_
