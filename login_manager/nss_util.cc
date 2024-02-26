// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/nss_util.h"

#include <stdint.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/notreached.h>
#include <base/strings/stringprintf.h>
#include <crypto/nss_util.h>
#include <crypto/nss_util_internal.h>
#include <crypto/rsa_private_key.h>
#include <crypto/scoped_nss_types.h>
#include <crypto/signature_creator.h>
#include <crypto/signature_verifier.h>
#include <brillo/scoped_mount_namespace.h>
#include <keyhi.h>
#include <pk11pub.h>
#include <prerror.h>
#include <secmod.h>
#include <secmodt.h>

using brillo::ScopedMountNamespace;

using crypto::ScopedPK11Slot;
using crypto::ScopedSECItem;
using crypto::ScopedSECKEYPrivateKey;
using crypto::ScopedSECKEYPublicKey;

namespace {
// This should match the same constant in Chrome tree:
// chromeos/dbus/constants/dbus_paths.cc
const char kOwnerKeyFile[] = "/var/lib/devicesettings/owner.key";

// TODO(hidehiko): Move this to scoped_nss_types.h.
struct CERTSubjectPublicKeyInfoDeleter {
  void operator()(CERTSubjectPublicKeyInfo* ptr) const {
    SECKEY_DestroySubjectPublicKeyInfo(ptr);
  }
};
using ScopedCERTSubjectPublicKeyInfo =
    std::unique_ptr<CERTSubjectPublicKeyInfo, CERTSubjectPublicKeyInfoDeleter>;

}  // namespace

namespace login_manager {
///////////////////////////////////////////////////////////////////////////
// NssUtil

NssUtil::NssUtil() = default;

NssUtil::~NssUtil() = default;

///////////////////////////////////////////////////////////////////////////
// NssUtilImpl

class NssUtilImpl : public NssUtil {
 public:
  NssUtilImpl();
  NssUtilImpl(const NssUtilImpl&) = delete;
  NssUtilImpl& operator=(const NssUtilImpl&) = delete;

  ~NssUtilImpl() override;

  base::FilePath GetOwnerKeyFilePath() override;

  bool CheckPublicKeyBlob(const std::vector<uint8_t>& blob) override;

  bool Verify(
      const std::vector<uint8_t>& signature,
      const std::vector<uint8_t>& data,
      const std::vector<uint8_t>& public_key,
      const crypto::SignatureVerifier::SignatureAlgorithm algorithm) override;

 private:
  static const uint16_t kKeySizeInBits;
};

// Defined here, instead of up above, because we need NssUtilImpl.
// static
std::unique_ptr<NssUtil> NssUtil::Create() {
  return std::make_unique<NssUtilImpl>();
}

// We're generating and using 2048-bit RSA keys.
// static
const uint16_t NssUtilImpl::kKeySizeInBits = 2048;

NssUtilImpl::NssUtilImpl() {
  if (setenv("NSS_SDB_USE_CACHE", "no", 1) == -1)
    PLOG(WARNING) << "Can't set NSS_SDB_USE_CACHE=no in the environment!";
  crypto::EnsureNSSInit();
}

NssUtilImpl::~NssUtilImpl() = default;

base::FilePath NssUtilImpl::GetOwnerKeyFilePath() {
  return base::FilePath(kOwnerKeyFile);
}

bool NssUtilImpl::CheckPublicKeyBlob(const std::vector<uint8_t>& blob) {
  SECItem spki_der;
  spki_der.type = siBuffer;
  spki_der.data = const_cast<uint8_t*>(&blob[0]);
  spki_der.len = blob.size();
  ScopedCERTSubjectPublicKeyInfo spki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&spki_der));
  if (!spki)
    return false;

  ScopedSECKEYPublicKey public_key(SECKEY_ExtractPublicKey(spki.get()));
  return static_cast<bool>(public_key);
}

// This is pretty much just a blind passthrough, so I won't test it
// in the NssUtil unit tests.  I'll test it from a class that uses this API.
bool NssUtilImpl::Verify(
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& public_key,
    const crypto::SignatureVerifier::SignatureAlgorithm algorithm) {
  crypto::SignatureVerifier verifier;

  if (!verifier.VerifyInit(algorithm, signature.data(), signature.size(),
                           public_key.data(), public_key.size())) {
    LOG(ERROR) << "Could not initialize verifier";
    return false;
  }

  verifier.VerifyUpdate(data.data(), data.size());
  return verifier.VerifyFinal();
}

}  // namespace login_manager
