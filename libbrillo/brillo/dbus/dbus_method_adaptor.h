// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_DBUS_DBUS_METHOD_ADAPTOR_H_
#define LIBBRILLO_BRILLO_DBUS_DBUS_METHOD_ADAPTOR_H_

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include <base/types/expected.h>
#include <dbus/error.h>
#include <dbus/message.h>

#include <brillo/dbus/data_serialization.h>
#include <brillo/dbus/dbus_method_response.h>

// Helpers to implement generated D-Bus Adaptor class.
namespace brillo::dbus_utils::details {

// Reads the inputs, invokes the generated adapter method,
// then write the outputs.
// `InputTuple` is the type of `std::tuple<...>` where ... is the list
// of the input types in order. Similarly, `OutputTuple` is the type of
// `std::tuple<...>` where ... is the list of the output types.
// `F` is the functor type, often expected to be an inline lambda created by
// the generator, taking `method_call` and the read arguments, and returns
// `base::expected<OutputTuple, dbus::Error>`.
template <typename InputTuple, typename OutputTuple, typename F>
void HandleSyncDBusMethod(dbus::MethodCall* method_call,
                          ResponseSender sender,
                          F&& f) {
  InputTuple input;
  dbus::MessageReader reader(method_call);
  if (!ApplyReadDBusArgs(&reader, input)) {
    return std::move(sender).Run(::dbus::ErrorResponse::FromMethodCall(
        method_call, DBUS_ERROR_INVALID_ARGS, "failed to read arguments"));
  }

  base::expected<OutputTuple, dbus::Error> result = std::apply(
      [method_call, &f](auto&&... args) mutable {
        return std::forward<F>(f)(method_call,
                                  std::forward<decltype(args)>(args)...);
      },
      std::move(input));
  if (!result.has_value()) {
    std::move(sender).Run(dbus::ErrorResponse::FromMethodCall(
        method_call, result.error().name(), result.error().message()));
    return;
  }

  auto response = dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  std::apply(
      [&writer](auto&&... args) {
        WriteDBusArgs(&writer, std::forward<decltype(args)>(args)...);
      },
      std::move(result.value()));
  std::move(sender).Run(std::move(response));
}

// Similar to HandleSyncDBusMethod, but handles async method.
// Instead of writing the output directly, this creates a `ResponseType`
// instance, which is typed `DBusMethodResponse<...>` where ... is the list
// of the output types in order, then passes it to `f`.
// `F` is a functor type, often an inline lambda created by the generator,
// which takes ResponseType, `method_call`, followed by the input arguments.
template <typename InputTuple, typename ResponseType, typename F>
void HandleAsyncDBusMethod(dbus::MethodCall* method_call,
                           ResponseSender sender,
                           F&& f) {
  static_assert(std::is_base_of_v<DBusMethodResponseBase, ResponseType>,
                "Response must be DBusMethodResponse<T...>");
  InputTuple input;
  dbus::MessageReader reader(method_call);
  if (!ApplyReadDBusArgs(&reader, input)) {
    return std::move(sender).Run(::dbus::ErrorResponse::FromMethodCall(
        method_call, DBUS_ERROR_INVALID_ARGS, "failed to read arguments"));
  }

  std::apply(
      [&method_call, &sender, &f](auto&&... args) mutable {
        std::forward<F>(f)(
            std::make_unique<ResponseType>(method_call, std::move(sender)),
            method_call, std::forward<decltype(args)>(args)...);
      },
      std::move(input));
}

}  // namespace brillo::dbus_utils::details

#endif  // LIBBRILLO_BRILLO_DBUS_DBUS_METHOD_ADAPTOR_H_
