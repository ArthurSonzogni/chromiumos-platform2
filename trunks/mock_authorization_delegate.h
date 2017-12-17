// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_MOCK_AUTHORIZATION_DELEGATE_H_
#define TRUNKS_MOCK_AUTHORIZATION_DELEGATE_H_

#include <string>

#include <base/basictypes.h>
#include <gmock/gmock.h>

#include "trunks/authorization_delegate.h"

namespace trunks {

class MockAuthorizationDelegate : public AuthorizationDelegate {
 public:
  MockAuthorizationDelegate();
  ~MockAuthorizationDelegate() override;

  MOCK_METHOD4(GetCommandAuthorization, bool(const std::string&,
                                             bool,
                                             bool,
                                             std::string*));
  MOCK_METHOD2(CheckResponseAuthorization, bool(const std::string&,
                                                const std::string&));
  MOCK_METHOD1(EncryptCommandParameter, bool(std::string*));
  MOCK_METHOD1(DecryptResponseParameter, bool(std::string*));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockAuthorizationDelegate);
};

}  // namespace trunks

#endif  // TRUNKS_MOCK_AUTHORIZATION_DELEGATE_H_
