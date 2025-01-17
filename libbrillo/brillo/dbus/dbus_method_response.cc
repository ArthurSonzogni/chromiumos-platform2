// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <brillo/dbus/dbus_method_response.h>

#include <brillo/dbus/utils.h>

namespace brillo {
namespace dbus_utils {

DBusMethodResponseBase::DBusMethodResponseBase(dbus::MethodCall* method_call,
                                               ResponseSender sender)
    : sender_(std::move(sender)), method_call_(method_call) {}

DBusMethodResponseBase::~DBusMethodResponseBase() {
  if (method_call_) {
    // Response hasn't been sent by the handler. Abort the call.
    Abort();
  }
}

void DBusMethodResponseBase::ReplyWithError(const brillo::Error* error) {
  CHECK(error);
  CheckCanSendResponse();
  dbus::Error dbus_error = ToDBusError(*error);
  SendRawResponse(dbus::ErrorResponse::FromMethodCall(
      method_call_, dbus_error.name(), dbus_error.message()));
}

void DBusMethodResponseBase::ReplyWithError(const base::Location& location,
                                            const std::string& error_domain,
                                            const std::string& error_code,
                                            const std::string& error_message) {
  ErrorPtr error;
  Error::AddTo(&error, location, error_domain, error_code, error_message);
  ReplyWithError(error.get());
}

void DBusMethodResponseBase::Abort() {
  SendRawResponse(std::unique_ptr<dbus::Response>());
}

void DBusMethodResponseBase::SendRawResponse(
    std::unique_ptr<dbus::Response> response) {
  CheckCanSendResponse();
  method_call_ = nullptr;  // Mark response as sent.
  std::move(sender_).Run(std::move(response));
}

std::unique_ptr<dbus::Response> DBusMethodResponseBase::CreateCustomResponse()
    const {
  return dbus::Response::FromMethodCall(method_call_);
}

bool DBusMethodResponseBase::IsResponseSent() const {
  return (method_call_ == nullptr);
}

void DBusMethodResponseBase::CheckCanSendResponse() const {
  CHECK(method_call_) << "Response already sent";
}

}  // namespace dbus_utils
}  // namespace brillo
