// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/error.h"

#include <base/files/file_path.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus-c++/error.h>

#include "shill/dbus_adaptor.h"
#include "shill/logging.h"

using std::string;

namespace shill {

// static
const Error::Info Error::kInfos[kNumErrors] = {
  { kErrorResultSuccess, "Success (no error)" },
  { kErrorResultFailure, "Operation failed (no other information)" },
  { kErrorResultAlreadyConnected, "Already connected" },
  { kErrorResultAlreadyExists, "Already exists" },
  { kErrorResultIncorrectPin, "Incorrect PIN" },
  { kErrorResultInProgress, "In progress" },
  { kErrorResultInternalError, "Internal error" },
  { kErrorResultInvalidApn, "Invalid APN" },
  { kErrorResultInvalidArguments, "Invalid arguments" },
  { kErrorResultInvalidNetworkName, "Invalid network name" },
  { kErrorResultInvalidPassphrase, "Invalid passphrase" },
  { kErrorResultInvalidProperty, "Invalid property" },
  { kErrorResultNoCarrier, "No carrier" },
  { kErrorResultNotConnected, "Not connected" },
  { kErrorResultNotFound, "Not found" },
  { kErrorResultNotImplemented, "Not implemented" },
  { kErrorResultNotOnHomeNetwork, "Not on home network" },
  { kErrorResultNotRegistered, "Not registered" },
  { kErrorResultNotSupported, "Not supported" },
  { kErrorResultOperationAborted, "Operation aborted" },
  { kErrorResultOperationInitiated, "Operation initiated" },
  { kErrorResultOperationTimeout, "Operation timeout" },
  { kErrorResultPassphraseRequired, "Passphrase required" },
  { kErrorResultPermissionDenied, "Permission denied" },
  { kErrorResultPinBlocked, "SIM PIN is blocked"},
  { kErrorResultPinRequired, "SIM PIN is required"},
  { kErrorResultWrongState, "Wrong state" }
};

Error::Error() {
  Reset();
}

Error::Error(Type type) {
  Populate(type);
}

Error::Error(Type type, const string& message) {
  Populate(type, message);
}

Error::~Error() {}

void Error::Populate(Type type) {
  Populate(type, GetDefaultMessage(type));
}

void Error::Populate(Type type, const string& message) {
  CHECK(type < kNumErrors) << "Error type out of range: " << type;
  type_ = type;
  message_ = message;
}

void Error::Reset() {
  Populate(kSuccess);
}

void Error::CopyFrom(const Error& error) {
  Populate(error.type_, error.message_);
}

bool Error::ToDBusError(::DBus::Error* error) const {
  if (IsFailure()) {
    error->set(GetDBusResult(type_).c_str(), message_.c_str());
    return true;
  } else {
    return false;
  }
}

// static
string Error::GetDBusResult(Type type) {
  CHECK(type < kNumErrors) << "Error type out of range: " << type;
  return kInfos[type].dbus_result;
}

// static
string Error::GetDefaultMessage(Type type) {
  CHECK(type < kNumErrors) << "Error type out of range: " << type;
  return kInfos[type].message;
}

// static
void Error::PopulateAndLog(const tracked_objects::Location& from_here,
                           Error* error, Type type, const string& message) {
  string file_name = base::FilePath(from_here.file_name()).BaseName().value();
  LOG(ERROR) << "[" << file_name << "("
             << from_here.line_number() << ")]: "<< message;
  if (error) {
    error->Populate(type, message);
  }
}

}  // namespace shill

std::ostream& operator<<(std::ostream& stream, const shill::Error& error) {
  stream << error.GetDBusResult(error.type()) << ": " << error.message();
  return stream;
}
