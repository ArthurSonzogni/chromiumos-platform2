// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/keygen_worker.h"

#include <unistd.h>
#include <sys/types.h>

#include <set>
#include <string>

#include <base/basictypes.h>
#include <base/file_util.h>
#include <base/logging.h>
#include <crypto/rsa_private_key.h>

#include "login_manager/nss_util.h"
#include "login_manager/policy_key.h"
#include "login_manager/system_utils.h"

namespace login_manager {

namespace keygen {

int GenerateKey(const std::string& filename, NssUtil* nss) {
  FilePath key_file(filename);
  PolicyKey key(key_file, nss);
  if (!key.PopulateFromDiskIfPossible())
    LOG(FATAL) << "Corrupted key on disk at " << filename;
  if (key.IsPopulated())
    LOG(FATAL) << "Existing owner key at " << filename;
  FilePath nssdb = key_file.DirName().Append(nss->GetNssdbSubpath());
  PLOG_IF(FATAL, !file_util::PathExists(nssdb)) << nssdb.value()
                                                << " does not exist!";
  if (!file_util::VerifyPathControlledByUser(key_file.DirName(),
                                             nssdb,
                                             getuid(),
                                             std::set<gid_t>())) {
    PLOG(FATAL) << nssdb.value() << " cannot be used by the user!";
  }
  PLOG_IF(FATAL,!nss->OpenUserDB()) << "Could not open/create user NSS DB";
  LOG(INFO) << "Generating Owner key.";

  scoped_ptr<crypto::RSAPrivateKey> pair(nss->GenerateKeyPair());
  if (pair.get()) {
    if (!key.PopulateFromKeypair(pair.get()))
      LOG(FATAL) << "Could not use generated keypair.";
    LOG(INFO) << "Writing Owner key to " << key_file.value();
    return (key.Persist() ? 0 : 1);
  }
  LOG(FATAL) << "Could not generate owner key!";
  return 0;
}

}  // namespace keygen

}  // namespace login_manager
