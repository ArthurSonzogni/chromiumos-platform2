// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUTHPOLICY_CRYPTOHOME_CLIENT_H_
#define AUTHPOLICY_CRYPTOHOME_CLIENT_H_

#include <memory>
#include <string>

#include <base/macros.h>

namespace org {
namespace chromium {
class CryptohomeInterfaceProxy;
}
}  // namespace org

namespace brillo {
namespace dbus_utils {
class DBusObject;
}
}  // namespace brillo

namespace authpolicy {

// Exposes methods from the Cryptohome daemon.
class CryptohomeClient {
 public:
  explicit CryptohomeClient(brillo::dbus_utils::DBusObject* dbus_object);
  virtual ~CryptohomeClient();

  // Exposes Cryptohome's GetSanitizedUsername(). This is a 32-byte lowercase
  // hex string that is also used as user directory. Returns an empty string on
  // error.
  std::string GetSanitizedUsername(const std::string& account_id_key);

 private:
  std::unique_ptr<org::chromium::CryptohomeInterfaceProxy> proxy_;

  DISALLOW_COPY_AND_ASSIGN(CryptohomeClient);
};

}  // namespace authpolicy

#endif  // AUTHPOLICY_CRYPTOHOME_CLIENT_H_
