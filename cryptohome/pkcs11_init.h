// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pkcs11Init - Class for handling PKCS #11 initialization.  Since the move to
// Chaps, this class does very little.  The loading / unloading of tokens is
// handled in mount.cc.

#ifndef CRYPTOHOME_PKCS11_INIT_H_
#define CRYPTOHOME_PKCS11_INIT_H_

#include <sys/types.h>

#include <string>

#include <base/basictypes.h>
#include <base/file_path.h>
#include <base/memory/scoped_ptr.h>
#include <chaps/pkcs11/cryptoki.h>
#include <glib.h>

#include "platform.h"

namespace cryptohome {

class Pkcs11Init {
 public:
  Pkcs11Init() : default_platform_(new Platform),
                 platform_(default_platform_.get()) {
  }
  virtual ~Pkcs11Init() { }

  virtual void GetTpmTokenInfo(gchar **OUT_label,
                               gchar **OUT_user_pin);

  virtual void GetTpmTokenInfoForUser(gchar *username,
                                      gchar **OUT_label,
                                      gchar **OUT_user_pin);

  // Returns the same label as GetTpmTokenInfoForUser.
  virtual std::string GetTpmTokenLabelForUser(const std::string& username);

  // Returns the tpm token slot for the given path.  If no slot is found,
  // returns the default slot (which will belong to the user who originally
  // signed in).
  virtual int GetTpmTokenSlotForPath(const base::FilePath& path);

  // Check if the user's PKCS #11 token is valid.
  virtual bool IsUserTokenBroken();

  static const CK_SLOT_ID kDefaultTpmSlotId;
  static const CK_ULONG kMaxLabelLen;
  static const CK_CHAR kDefaultUserPin[];
  static const CK_CHAR kDefaultLabel[];

 private:
  // Returns true if a token in the given |slot_id| passes basic sanity checks.
  // This includes checking if the |expected_label| matches the actual token
  // label.
  bool CheckTokenInSlot(CK_SLOT_ID slot_id, const CK_CHAR* expected_label);

  scoped_ptr<Platform> default_platform_;
  Platform* platform_;

  DISALLOW_COPY_AND_ASSIGN(Pkcs11Init);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PKCS11_INIT_H_
