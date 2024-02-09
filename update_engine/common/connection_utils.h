// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_CONNECTION_UTILS_H_
#define UPDATE_ENGINE_COMMON_CONNECTION_UTILS_H_

#include <string>

namespace chromeos_update_engine {

enum class ConnectionType {
  kDisconnected,
  kEthernet,
  kWifi,
  kCellular,
  kUnknown
};

namespace connection_utils {
// Helper methods for converting shill strings into symbolic values.
ConnectionType ParseConnectionType(const std::string& type_str);

// Returns the string representation corresponding to the given connection type.
const char* StringForConnectionType(ConnectionType type);
}  // namespace connection_utils

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CONNECTION_UTILS_H_
