// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_STORE_PKCS11_UTIL_H_
#define SHILL_STORE_PKCS11_UTIL_H_

#include <chaps/pkcs11/cryptoki.h>

#include <string>

namespace shill {

namespace pkcs11 {
// A helper class to scope a PKCS #11 session.
class ScopedSession {
 public:
  explicit ScopedSession(CK_SLOT_ID slot);
  ScopedSession(const ScopedSession&) = delete;
  ScopedSession& operator=(const ScopedSession&) = delete;
  ~ScopedSession();

  CK_SESSION_HANDLE handle() const { return handle_; }

  bool IsValid() const { return (handle_ != CK_INVALID_HANDLE); }

 private:
  CK_SESSION_HANDLE handle_ = CK_INVALID_HANDLE;
};

// Set the content of |slot| with the slot id for the given |user_hash| or the
// system slot if |user_hash| is empty. Return false if no appropriate slot is
// found.
bool GetUserSlot(const std::string& user_hash, CK_SLOT_ID_PTR slot);

}  // namespace pkcs11

}  // namespace shill

#endif  // SHILL_STORE_PKCS11_UTIL_H_
