// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBARC_ATTESTATION_LIB_VERSION_ATTESTER_H_
#define LIBARC_ATTESTATION_LIB_VERSION_ATTESTER_H_

#include <memory>
#include <string>

#include <brillo/secure_blob.h>
#include <libarc-attestation/lib/interface.h>
#include <libarc-attestation/lib/provisioner.h>
#include <libarc_attestation/proto_bindings/arc_attestation_blob.pb.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/frontend/attestation/frontend.h>

namespace arc_attestation {

// VersionAttester is in charge of attesting the version of the device.
// This is usually done through libhwsec.
class VersionAttester {
 public:
  explicit VersionAttester(Provisioner* provisioner);
  ~VersionAttester() = default;

  // This will provide an attestation blob.
  // This must be called on the library thread from AttestationInterface.
  AndroidStatus QuoteCrOSBlob(const brillo::Blob& challenge,
                              brillo::Blob& output);

  void SetHwsecFactoryForTesting(hwsec::Factory* hwsec_factory) {
    hwsec_factory_ = hwsec_factory;
  }

 private:
  // Setup the libhwsec factory.
  bool InitHwsec();

  // Provisioner to access the certificates.
  Provisioner* provisioner_;

  // The instance of hwsec factory for accessing hwsec in production.
  std::unique_ptr<hwsec::FactoryImpl> default_hwsec_factory_;
  // The instance of hwsec factory actually used, usually the same as
  // default_hwsec_factory_, but can be overridden for testing.
  hwsec::Factory* hwsec_factory_;
  // The instance of hwsec frontend used.
  std::unique_ptr<const hwsec::ArcAttestationFrontend> hwsec_frontend_;
};

}  // namespace arc_attestation

#endif  // LIBARC_ATTESTATION_LIB_VERSION_ATTESTER_H_
