// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CLIENT_ID_CLIENT_ID_H_
#define CLIENT_ID_CLIENT_ID_H_

#include <string>

#include <base/files/file_util.h>
#include <base/optional.h>

namespace client_id {

// This class is responsible for reading various sources to determine
// and save a unique machine identifier.
class ClientIdGenerator {
 public:
  explicit ClientIdGenerator(const base::FilePath& base_path);

  // Can be used to add a prefix to the client_id.
  base::Optional<std::string> AddClientIdPrefix(const std::string& client_id);

  // Reads the contents of var/lib/client_id/client_id which is
  // the client_id.
  base::Optional<std::string> ReadClientId();

  // Reads the contents of mnt/stateful_partition/cloudready/client_id
  // which is the legacy CloudReady client_id
  base::Optional<std::string> TryLegacy();

  // Reads the contents of sys/devices/virtual/dmi/id/product_serial
  // The serial is compared against known bad values and other criteria
  // If successful, the prefix is added and the result is returned
  base::Optional<std::string> TrySerial();

  // Tries to find a hardware mac address from sys/class/net
  // The interfaces are compared against known good/bad names, addresses,
  // and what bus the device is on. If successful, the prefix is added
  // and the result is returned.
  base::Optional<std::string> TryMac();

  // Reads the contents of proc/sys/kernel/random/uuid. This is a random id.
  // If successful, the prefix is added and the result is returned
  base::Optional<std::string> TryUuid();

  // Writes the client_id to var/lib/client_id/client_id
  // with a newline.
  bool WriteClientId(const std::string& client_id);

  // Tries to find the best client id in the order:
  // 1. Client ID
  // 2. Legacy Client ID
  // 3. DMI Serial Number
  // 4. Hardware MAC Address
  // 5. Random UUID
  // The result is saved to var/lib/client_id/client_id
  base::Optional<std::string> GenerateAndSaveClientId();

 private:
  base::FilePath base_path_;
};

}  // namespace client_id

#endif  // CLIENT_ID_CLIENT_ID_H_
