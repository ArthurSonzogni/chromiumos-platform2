// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_IMPL_H_
#define CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>
#include <libhwsec/frontend/recovery_crypto/frontend.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/cryptorecovery/recovery_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"
#include "cryptohome/cryptorecovery/recovery_id_container.pb.h"
#include "cryptohome/proto_bindings/rpc.pb.h"

namespace cryptohome::cryptorecovery {
// Cryptographic operations for cryptohome recovery performed on either CPU
// (software emulation) or TPM modules depending on the TPM backend.
class RecoveryCryptoImpl : public RecoveryCrypto {
 public:
  // Creates instance. Returns nullptr if error occurred.
  static std::unique_ptr<RecoveryCryptoImpl> Create(
      const hwsec::RecoveryCryptoFrontend* hwsec_backend,
      libstorage::Platform* platform);

  RecoveryCryptoImpl(const RecoveryCryptoImpl&) = delete;
  RecoveryCryptoImpl& operator=(const RecoveryCryptoImpl&) = delete;

  ~RecoveryCryptoImpl() override;

  bool GenerateRecoveryRequest(
      const GenerateRecoveryRequestRequest& request_param,
      CryptoRecoveryRpcRequest* recovery_request,
      brillo::Blob* ephemeral_pub_key) const override;
  bool GenerateHsmPayload(const GenerateHsmPayloadRequest& request,
                          GenerateHsmPayloadResponse* response) const override;
  bool RecoverDestination(const RecoverDestinationRequest& request,
                          brillo::SecureBlob* destination_dh) const override;
  CryptoStatus DecryptResponsePayload(
      const DecryptResponsePayloadRequest& request,
      HsmResponsePlainText* response_plain_text) const override;

  [[nodiscard]] bool GenerateOnboardingMetadata(
      const std::string& gaia_id,
      const std::string& user_device_id,
      const std::string& recovery_id,
      const brillo::Blob& mediator_pub_key,
      OnboardingMetadata* onboarding_metadata) const;
  // Gets the current serialized value of the Recovery Id from cryptohome or
  // returns an empty string if it does not exist. It should be called by the
  // client before GenerateOnboardingMetadata in order to get the recovery_id
  // that will be passed as an argument.
  std::string LoadStoredRecoveryIdFromFile(
      const base::FilePath& recovery_id_path) const;
  std::string LoadStoredRecoverySeedFromFile(
      const base::FilePath& recovery_id_path) const;
  std::string LoadStoredRecoveryId(const AccountIdentifier& account_id) const;
  std::string LoadStoredRecoverySeed(const AccountIdentifier& account_id) const;
  [[nodiscard]] bool LoadPersistedRecoveryIdContainer(
      const base::FilePath& recovery_id_path,
      CryptoRecoveryIdContainer* recovery_id_pb) const;

  // This method should be called on the initial creation of OnboardingMetadata
  // and after every successful recovery operation to refresh the Recovery Id.
  // Secrets used to generate Recovery Id are stored in cryptohome but the
  // resulting Recovery Id is part of OnboardingMetadata stored outside of the
  // cryptohome.
  // Creates a random seed and computes Recovery Id from it.
  // If Recovery Id already exists - noop.
  [[nodiscard]] bool EnsureRecoveryIdPresent(
      const AccountIdentifier& account_id) const;
  // Creates a random seed and computes Recovery Id from it.
  // If Recovery Id already exists - re-hashes and persists it in the
  // cryptohome.
  [[nodiscard]] bool GenerateFreshRecoveryId(
      const AccountIdentifier& account_id) const;
  [[nodiscard]] bool GenerateRecoveryIdToFiles(
      const base::FilePath& recovery_container_path,
      const base::FilePath& recovery_id_path) const;

  // Returns a vector of last |max_depth| Recovery ids. The current recovery_id
  // is returned as the first entry.
  std::vector<std::string> GetLastRecoveryIds(
      const AccountIdentifier& account_id, int max_depth) const;

 private:
  RecoveryCryptoImpl(hwsec_foundation::EllipticCurve ec,
                     const hwsec::RecoveryCryptoFrontend* hwsec_backend,
                     libstorage::Platform* platform);
  [[nodiscard]] bool GetMediatorPubKeyHash(
      const brillo::Blob& mediator_pub_key_spki_der, brillo::Blob* hash) const;
  [[nodiscard]] bool GenerateRecoveryKey(
      const crypto::ScopedEC_POINT& recovery_pub_point,
      const crypto::ScopedEC_KEY& dealer_key_pair,
      brillo::SecureBlob* recovery_key) const;
  // Generate ephemeral public and inverse public keys {G*x, G*-x}
  [[nodiscard]] bool GenerateEphemeralKey(
      brillo::Blob* ephemeral_spki_der,
      brillo::Blob* ephemeral_inv_spki_der) const;
  [[nodiscard]] bool GenerateHsmAssociatedData(
      const brillo::Blob& channel_pub_key,
      const brillo::Blob& rsa_pub_key,
      const crypto::ScopedEC_KEY& publisher_key_pair,
      const OnboardingMetadata& onboarding_metadata,
      brillo::Blob* hsm_associated_data) const;
  [[nodiscard]] bool IsRecoveryIdAvailable(
      const base::FilePath& recovery_id_path) const;
  [[nodiscard]] bool RotateRecoveryId(
      CryptoRecoveryIdContainer* recovery_id_pb) const;
  void GenerateInitialRecoveryId(
      CryptoRecoveryIdContainer* recovery_id_pb) const;
  void GenerateRecoveryIdProto(CryptoRecoveryIdContainer* recovery_id_pb) const;
  [[nodiscard]] bool PersistRecoveryIdContainer(
      const base::FilePath& recovery_id_path,
      const CryptoRecoveryIdContainer& recovery_id_pb) const;
  std::vector<std::string> GetLastRecoveryIdsFromFile(
      const base::FilePath& recovery_id_path, int max_depth) const;

  hwsec_foundation::EllipticCurve ec_;
  const hwsec::RecoveryCryptoFrontend* const hwsec_backend_;
  libstorage::Platform* const platform_;
};

}  // namespace cryptohome::cryptorecovery

#endif  // CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_IMPL_H_
