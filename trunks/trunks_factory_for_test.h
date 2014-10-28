// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TRUNKS_FACTORY_FOR_TEST_H_
#define TRUNKS_TRUNKS_FACTORY_FOR_TEST_H_

#include "trunks/trunks_factory.h"

#include <string>

#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <chromeos/chromeos_export.h>

namespace trunks {

class AuthorizationDelegate;
class AuthorizationSession;
class MockAuthorizationSession;
class MockTpm;
class MockTpmState;
class MockTpmUtility;
class NullAuthorizationDelegate;
class Tpm;
class TpmState;
class TpmUtility;

// A factory implementation for testing. Custom instances can be injected. If no
// instance has been injected, a default mock instance will be used. Objects for
// which ownership is passed to the caller are instantiated as forwarders which
// simply forward calls to the current instance set for the class.
//
// Example usage:
//   TrunksFactoryForTest factory;
//   MockTpmState mock_tpm_state;
//   factory.set_tpm_state(mock_tpm_state);
//   // Set expectations on mock_tpm_state...
class CHROMEOS_EXPORT TrunksFactoryForTest : public TrunksFactory {
 public:
  TrunksFactoryForTest();
  virtual ~TrunksFactoryForTest();

  // TrunksFactory methods.
  Tpm* GetTpm() const override;
  scoped_ptr<TpmState> GetTpmState() const override;
  scoped_ptr<TpmUtility> GetTpmUtility() const override;
  scoped_ptr<AuthorizationDelegate> GetPasswordAuthorization(
      const std::string& password) const override;
  scoped_ptr<AuthorizationSession> GetAuthorizationSession() const override;

  // Mutators to inject custom mocks.
  void set_tpm(Tpm* tpm) {
    tpm_ = tpm;
  }

  void set_tpm_state(TpmState* tpm_state) {
    tpm_state_ = tpm_state;
  }

  void set_tpm_utility(TpmUtility* tpm_utility) {
    tpm_utility_ = tpm_utility;
  }

  void set_password_authorization_delegate(AuthorizationDelegate* delegate) {
    password_authorization_delegate_ = delegate;
  }

  void set_authorization_session(AuthorizationSession* session) {
    authorization_session_ = session;
  }

 private:
  scoped_ptr<MockTpm> default_tpm_;
  Tpm* tpm_;
  scoped_ptr<MockTpmState> default_tpm_state_;
  TpmState* tpm_state_;
  scoped_ptr<MockTpmUtility> default_tpm_utility_;
  TpmUtility* tpm_utility_;
  scoped_ptr<NullAuthorizationDelegate> default_authorization_delegate_;
  AuthorizationDelegate* password_authorization_delegate_;
  scoped_ptr<MockAuthorizationSession> default_authorization_session_;
  AuthorizationSession* authorization_session_;

  DISALLOW_COPY_AND_ASSIGN(TrunksFactoryForTest);
};

}  // namespace trunks

#endif  // TRUNKS_TRUNKS_FACTORY_FOR_TEST_H_
