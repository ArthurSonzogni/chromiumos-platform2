// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_MOCK_SESSION_MANAGER_H_
#define TRUNKS_MOCK_SESSION_MANAGER_H_

#include "trunks/session_manager.h"

#include <string>

#include <gmock/gmock.h>

namespace trunks {

class MockSessionManager : public SessionManager {
 public:
  MockSessionManager();
  ~MockSessionManager() override;

  MOCK_CONST_METHOD0(GetSessionHandle, TPM_HANDLE());
  MOCK_METHOD0(CloseSession, void());
  MOCK_METHOD5(StartSession, TPM_RC(TPM_SE,
                                    TPMI_DH_ENTITY,
                                    const std::string&,
                                    bool,
                                    HmacAuthorizationDelegate*));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSessionManager);
};

}  // namespace trunks

#endif  // TRUNKS_MOCK_SESSION_MANAGER_H_
