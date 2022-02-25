// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_USER_SESSION_FACTORY_H_
#define CRYPTOHOME_USER_SESSION_USER_SESSION_FACTORY_H_

#include <memory>

#include <base/memory/ref_counted.h>

#include "cryptohome/user_session/user_session.h"

namespace cryptohome {

class UserSessionFactory {
 public:
  UserSessionFactory() = default;
  virtual ~UserSessionFactory() = default;

  virtual scoped_refptr<UserSession> New(bool, bool) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_USER_SESSION_FACTORY_H_
