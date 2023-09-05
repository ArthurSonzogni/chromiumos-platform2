// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides a way to call D-Bus methods on objects in remote processes
// as if they were native C++ function calls.

// CallMethodAndBlock (along with CallMethodAndBlockWithTimeout) lets you call
// a D-Bus method synchronously and pass all the required parameters as C++
// function arguments. CallMethodAndBlock relies on automatic C++ to D-Bus data
// serialization implemented in brillo/dbus/data_serialization.h.
// CallMethodAndBlock invokes the D-Bus method and returns the Response.

// The method call response should be parsed with ExtractMethodCallResults().
// The method takes an optional list of pointers to the expected return values
// of the D-Bus method.

// CallMethod and CallMethodWithTimeout are similar to CallMethodAndBlock but
// make the calls asynchronously. They take two callbacks: one for successful
// method invocation and the second is for error conditions.

// Here is an example of synchronous calls:
// Call "std::string MyInterface::MyMethod(int, double)" over D-Bus:

//  using brillo::dbus_utils::CallMethodAndBlock;
//  using brillo::dbus_utils::ExtractMethodCallResults;
//
//  brillo::ErrorPtr error;
//  auto resp = CallMethodAndBlock(obj,
//                                 "org.chromium.MyService.MyInterface",
//                                 "MyMethod",
//                                 &error,
//                                 2, 8.7);
//  std::string return_value;
//  if (resp && ExtractMethodCallResults(resp.get(), &error, &return_value)) {
//    // Use the |return_value|.
//  } else {
//    // An error occurred. Use |error| to get details.
//  }

// And here is how to call D-Bus methods asynchronously:
// Call "std::string MyInterface::MyMethod(int, double)" over D-Bus:

//  using brillo::dbus_utils::CallMethod;
//  using brillo::dbus_utils::ExtractMethodCallResults;
//
//  void OnSuccess(const std::string& return_value) {
//    // Use the |return_value|.
//  }
//
//  void OnError(brillo::Error* error) {
//    // An error occurred. Use |error| to get details.
//  }
//
//  brillo::dbus_utils::CallMethod(obj,
//                                   "org.chromium.MyService.MyInterface",
//                                   "MyMethod",
//                                   base::BindOnce(OnSuccess),
//                                   base::BindOnce(OnError),
//                                   2, 8.7);

#ifndef LIBBRILLO_BRILLO_DBUS_DBUS_METHOD_INVOKER_H_
#define LIBBRILLO_BRILLO_DBUS_DBUS_METHOD_INVOKER_H_

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <brillo/dbus/data_serialization.h>
#include <brillo/dbus/utils.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <brillo/brillo_export.h>
#include <dbus/error.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace brillo {
namespace dbus_utils {

// A helper method to dispatch a blocking D-Bus method call. Can specify
// zero or more method call arguments in |args| which will be sent over D-Bus.
// This method sends a D-Bus message and blocks for a time period specified
// in |timeout_ms| while waiting for a reply. The time out is in milliseconds or
// -1 (DBUS_TIMEOUT_USE_DEFAULT) for default, or DBUS_TIMEOUT_INFINITE for no
// timeout. If a timeout occurs, the response object contains an error object
// with DBUS_ERROR_NO_REPLY error code (those constants come from libdbus
// [dbus/dbus.h]).
// Returns a dbus::Response object on success. On failure, returns nullptr and
// fills in additional error details into the |error| object.
template <typename... Args>
inline std::unique_ptr<::dbus::Response> CallMethodAndBlockWithTimeout(
    int timeout_ms,
    ::dbus::ObjectProxy* object,
    const std::string& interface_name,
    const std::string& method_name,
    ErrorPtr* error,
    const Args&... args) {
  ::dbus::MethodCall method_call(interface_name, method_name);
  // Add method arguments to the message buffer.
  ::dbus::MessageWriter writer(&method_call);
  WriteDBusArgs(&writer, args...);
  auto response = object->CallMethodAndBlock(&method_call, timeout_ms);
  if (!response.has_value()) {
    ::dbus::Error dbus_error = std::move(response.error());
    if (dbus_error.IsValid()) {
      Error::AddToPrintf(
          error, FROM_HERE, errors::dbus::kDomain, dbus_error.name(),
          "Error calling D-Bus method: %s.%s: %s", interface_name.c_str(),
          method_name.c_str(), dbus_error.message().c_str());
    } else {
      Error::AddToPrintf(error, FROM_HERE, errors::dbus::kDomain,
                         DBUS_ERROR_FAILED,
                         "Failed to call D-Bus method: %s.%s",
                         interface_name.c_str(), method_name.c_str());
    }
    return nullptr;
  }
  return std::move(response.value());
}

// Same as CallMethodAndBlockWithTimeout() but uses a default timeout value.
template <typename... Args>
inline std::unique_ptr<::dbus::Response> CallMethodAndBlock(
    ::dbus::ObjectProxy* object,
    const std::string& interface_name,
    const std::string& method_name,
    ErrorPtr* error,
    const Args&... args) {
  return CallMethodAndBlockWithTimeout(::dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                                       object, interface_name, method_name,
                                       error, args...);
}

// A helper method to extract a list of values from a message buffer.
// The function will return false and provide detailed error information on
// failure. It fails if the D-Bus message buffer (represented by the |reader|)
// contains too many, too few parameters or the parameters are of wrong types
// (signatures).
// The usage pattern is as follows:
//
//  int32_t data1;
//  std::string data2;
//  ErrorPtr error;
//  if (ExtractMessageParameters(reader, &error, &data1, &data2)) { ... }
//
// The above example extracts an Int32 and a String from D-Bus message buffer.
template <typename... ResultTypes>
inline bool ExtractMessageParameters(::dbus::MessageReader* reader,
                                     ErrorPtr* error,
                                     ResultTypes*... results) {
  if (!ReadDBusArgs(reader, results...)) {
    *error = Error::Create(FROM_HERE, errors::dbus::kDomain,
                           DBUS_ERROR_INVALID_ARGS, "Failed to read params");
    return false;
  }

  return true;
}

// Convenient helper method to extract return value(s) of a D-Bus method call.
// |results| must be zero or more pointers to data expected to be returned
// from the method called. If an error occurs, returns false and provides
// additional details in |error| object.
//
// It is OK to call this method even if the D-Bus method doesn't expect
// any return values. Just do not specify any output |results|. In this case,
// ExtractMethodCallResults() will verify that the method didn't return any
// data in the |message|.
template <typename... ResultTypes>
inline bool ExtractMethodCallResults(::dbus::Message* message,
                                     ErrorPtr* error,
                                     ResultTypes*... results) {
  CHECK(message) << "Unable to extract parameters from a NULL message.";

  ::dbus::MessageReader reader(message);
  if (message->GetMessageType() == ::dbus::Message::MESSAGE_ERROR) {
    std::string error_message;
    if (ExtractMessageParameters(&reader, error, &error_message))
      AddDBusError(error, message->GetErrorName(), error_message);
    return false;
  }
  return ExtractMessageParameters(&reader, error, results...);
}

//////////////////////////////////////////////////////////////////////////////
// Asynchronous method invocation support

using AsyncErrorCallback = base::OnceCallback<void(Error* error)>;

// A helper function that translates dbus::ErrorResponse response
// from D-Bus into brillo::Error* and invokes the |callback|.
void BRILLO_EXPORT TranslateErrorResponse(AsyncErrorCallback callback,
                                          ::dbus::ErrorResponse* resp);

// A helper function that translates dbus::Response from D-Bus into
// a list of C++ values passed as parameters to |success_callback|. If the
// response message doesn't have the correct number of parameters, or they
// are of wrong types, an error is sent to |error_callback|.
template <typename... OutArgs>
void TranslateSuccessResponse(
    base::OnceCallback<void(OutArgs...)> success_callback,
    AsyncErrorCallback error_callback,
    ::dbus::Response* resp) {
  std::tuple<StorageType<OutArgs>...> tuple;
  ::dbus::MessageReader reader(resp);
  if (!ApplyReadDBusArgs(&reader, tuple)) {
    std::move(error_callback)
        .Run(Error::Create(FROM_HERE, errors::dbus::kDomain,
                           DBUS_ERROR_INVALID_ARGS, "Failed to read params")
                 .get());
    return;
  }
  std::apply(
      [&success_callback](auto&&... args) {
        std::move(success_callback).Run(std::forward<decltype(args)>(args)...);
      },
      std::move(tuple));
}

// A helper method to dispatch a non-blocking D-Bus method call. Can specify
// zero or more method call arguments in |params| which will be sent over D-Bus.
// This method sends a D-Bus message and returns immediately.
// When the remote method returns successfully, the success callback is
// invoked with the return value(s), if any.
// On error, the error callback is called. Note, the error callback can be
// called synchronously (before CallMethodWithTimeout returns) if there was
// a problem invoking a method (e.g. object or method doesn't exist).
// If the response is not received within |timeout_ms|, an error callback is
// called with DBUS_ERROR_NO_REPLY error code.
template <typename... InArgs, typename... OutArgs>
inline void CallMethodWithTimeout(
    int timeout_ms,
    ::dbus::ObjectProxy* object,
    const std::string& interface_name,
    const std::string& method_name,
    base::OnceCallback<void(OutArgs...)> success_callback,
    AsyncErrorCallback error_callback,
    const InArgs&... params) {
  ::dbus::MethodCall method_call(interface_name, method_name);
  ::dbus::MessageWriter writer(&method_call);
  WriteDBusArgs(&writer, params...);

  auto split_error_callback =
      base::SplitOnceCallback(std::move(error_callback));

  ::dbus::ObjectProxy::ErrorCallback dbus_error_callback = base::BindOnce(
      &TranslateErrorResponse, std::move(split_error_callback.first));
  ::dbus::ObjectProxy::ResponseCallback dbus_success_callback = base::BindOnce(
      &TranslateSuccessResponse<OutArgs...>, std::move(success_callback),
      std::move(split_error_callback.second));

  object->CallMethodWithErrorCallback(&method_call, timeout_ms,
                                      std::move(dbus_success_callback),
                                      std::move(dbus_error_callback));
}

// Same as CallMethodWithTimeout() but uses a default timeout value.
template <typename... InArgs, typename... OutArgs>
inline void CallMethod(::dbus::ObjectProxy* object,
                       const std::string& interface_name,
                       const std::string& method_name,
                       base::OnceCallback<void(OutArgs...)> success_callback,
                       AsyncErrorCallback error_callback,
                       const InArgs&... params) {
  return CallMethodWithTimeout(::dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, object,
                               interface_name, method_name,
                               std::move(success_callback),
                               std::move(error_callback), params...);
}

}  // namespace dbus_utils
}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_DBUS_DBUS_METHOD_INVOKER_H_
