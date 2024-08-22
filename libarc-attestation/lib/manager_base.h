// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBARC_ATTESTATION_LIB_MANAGER_BASE_H_
#define LIBARC_ATTESTATION_LIB_MANAGER_BASE_H_

#include <memory>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>
#include <libarc-attestation/lib/interface.h>

namespace arc_attestation {

class ARC_ATTESTATION_EXPORT ArcAttestationManagerBase {
 public:
  ArcAttestationManagerBase() = default;
  virtual ~ArcAttestationManagerBase() = default;

  // Setup the threading and other variables.
  virtual void Setup() = 0;

  // Provision the Device Key certificate chain.
  virtual AndroidStatus ProvisionDkCert(bool blocking) = 0;

  // Retrieve the Device Key certs.
  virtual AndroidStatus GetDkCertChain(std::vector<brillo::Blob>& cert_out) = 0;

  // Sign with the device key.
  virtual AndroidStatus SignWithP256Dk(const brillo::Blob& input,
                                       brillo::Blob& signature) = 0;

  // Quote the ChromeOS specific blob for ARC Attestation.
  virtual AndroidStatus QuoteCrOSBlob(const brillo::Blob& challenge,
                                      brillo::Blob& output) = 0;

  // Get Endorsement Public Key from ChromeOS attestation service.
  virtual AndroidStatus GetEndorsementPublicKey(brillo::Blob& output) = 0;
};

class ARC_ATTESTATION_EXPORT ArcAttestationManagerSingleton {
 public:
  // Retrieve the singleton.
  // This is not thread safe and should only be accessed from the API functions
  // because the caller is required to call these functions without
  // overlapping.
  static ArcAttestationManagerSingleton* Get();

  static ArcAttestationManagerSingleton* CreateForTesting();

  static void DestroyForTesting();

  void SetManagerForTesting(std::unique_ptr<ArcAttestationManagerBase> manager);
  ArcAttestationManagerBase* manager();

 private:
  // The global singleton instance of attestation interface.
  static ArcAttestationManagerSingleton* g_instance;

  // Private constructor because it's singleton.
  ArcAttestationManagerSingleton();

  // Setup the manager.
  void Setup();

  // The manager instance.
  std::unique_ptr<ArcAttestationManagerBase> manager_;
};

}  // namespace arc_attestation

#endif  // LIBARC_ATTESTATION_LIB_MANAGER_BASE_H_
