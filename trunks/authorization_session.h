// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_AUTHORIZATION_SESSION_H_
#define TRUNKS_AUTHORIZATION_SESSION_H_

#include <string>

#include <base/macros.h>

#include "trunks/tpm_generated.h"

namespace trunks {

class AuthorizationDelegate;

// AuthorizationSession is an interface for managing sessions for authorization
// and parameter encryption.
class AuthorizationSession {
 public:
  AuthorizationSession() {}
  virtual ~AuthorizationSession() {}

  // Returns an authorization delegate for this session. Ownership of the
  // delegate pointer is retained by the session.
  virtual AuthorizationDelegate* GetDelegate() = 0;

  // Starts a salted session which is bound to |bind_entity| with
  // |bind_authorization_value|. Encryption is enabled if |enable_encryption| is
  // true. The session remains active until this object is destroyed or another
  // session is started with a call to Start*Session.
  virtual TPM_RC StartBoundSession(
      TPMI_DH_ENTITY bind_entity,
      const std::string& bind_authorization_value,
      bool enable_encryption) = 0;

  // Starts a salted, unbound session. Encryption is enabled if
  // |enable_encryption| is true. The session remains active until this object
  // is destroyed or another session is started with a call to Start*Session.
  virtual TPM_RC StartUnboundSession(bool enable_encryption) = 0;

  // Sets the current entity authorization value. This can be safely called
  // while the session is active and subsequent commands will use the value.
  virtual void SetEntityAuthorizationValue(const std::string& value) = 0;

  // Sets the future_authorization_value field in the HmacDelegate. This
  // is used in response validation for the TPM2_HierarchyChangeAuth command.
  // We need to perform this because the HMAC value returned from
  // HierarchyChangeAuth uses the new auth_value.
  virtual void SetFutureAuthorizationValue(const std::string& value) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(AuthorizationSession);
};

}  // namespace trunks

#endif  // TRUNKS_AUTHORIZATION_SESSION_H_
