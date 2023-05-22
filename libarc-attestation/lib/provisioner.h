// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBARC_ATTESTATION_LIB_PROVISIONER_H_
#define LIBARC_ATTESTATION_LIB_PROVISIONER_H_

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <attestation/proto_bindings/interface.pb.h>
#include <attestation-client/attestation/dbus-proxies.h>
#include <base/threading/thread.h>
#include <dbus/bus.h>

#include <libarc-attestation/lib/interface.h>

namespace arc_attestation {

// Provisioner provisions the certificate with attestationd.
class Provisioner {
 public:
  explicit Provisioner(scoped_refptr<base::SingleThreadTaskRunner> runner)
      : runner_(runner), provisioned_(false) {}

  // Return if we've the certs provisioned.
  // This can be called from any thread and is thread-safe.
  bool is_provisioned() { return provisioned_.load(); }

  // Call this to provision the certificates.
  AndroidStatus ProvisionCert();

  // Call this to retrieve the DK certificates.
  // This must be invoked on the task runner.
  AndroidStatus GetDkCertChain(std::vector<std::vector<uint8_t>>& cert_out);

  // Call this to sign with the Android Device Key (DK).
  // This must be invoked on the task runner.
  AndroidStatus SignWithP256Dk(const std::vector<uint8_t>& input,
                               std::vector<uint8_t>& signature);

  // Call this to obtain the ARC TPM Certifying Key's key blob.
  // This must be invoked on the task runner.
  std::optional<std::string> GetTpmCertifyingKeyBlob();

  // Call this to obtain the ARC TPM Certifying Key's certificate.
  // This must be invoked on the task runner.
  std::optional<std::string> GetTpmCertifyingKeyCert();

  void SetAttestationProxyForTesting(
      std::unique_ptr<org::chromium::AttestationProxyInterface> proxy) {
    proxy_ = std::move(proxy);
  }

 private:
  // Ensures that the dbus connection is ready.
  // This must be called from runner_.
  bool EnsureDbus();

  // Internal method used by EnsureDbus() to simply retry.
  bool EnsureDbusInternal();

  // Returns true if we're on the same thread as runner_.
  bool IsOnRunner();

  // Provision the TPM Certifying Key.
  AndroidStatus ProvisionCertifyingKey();

  // Provision the ARC Attestation Device Key.
  AndroidStatus ProvisionArcAttestationDeviceKey();

  // All provisioning related tasks runs on this task runner.
  scoped_refptr<base::SingleThreadTaskRunner> runner_;

  // The dbus connection, this is guaranteed to be available if EnsureDbus()
  // returns true.
  scoped_refptr<::dbus::Bus> bus_;

  // DBus proxy for accessing the attestation service.
  std::unique_ptr<org::chromium::AttestationProxyInterface> proxy_;

  // Set to true if the certs are provisioned.
  std::atomic<bool> provisioned_;

  // The data regarding TPM Certifying Key.
  std::optional<attestation::GetCertificateReply> tck_data_;

  // The data regarding ARC Attestation Device Key.
  std::optional<attestation::GetCertificateReply> aadk_data_;
};

}  // namespace arc_attestation

#endif  // LIBARC_ATTESTATION_LIB_PROVISIONER_H_
