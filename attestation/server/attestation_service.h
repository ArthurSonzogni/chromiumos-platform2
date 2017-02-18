//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ATTESTATION_SERVER_ATTESTATION_SERVICE_H_
#define ATTESTATION_SERVER_ATTESTATION_SERVICE_H_

#include "attestation/common/attestation_interface.h"

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/threading/thread.h>
#include <brillo/bind_lambda.h>
#include <brillo/http/http_transport.h>

#include "attestation/common/attestation_ca.pb.h"
#include "attestation/common/crypto_utility.h"
#include "attestation/common/crypto_utility_impl.h"
#include "attestation/common/tpm_utility_factory.h"
#include "attestation/server/database.h"
#include "attestation/server/database_impl.h"
#include "attestation/server/key_store.h"
#include "attestation/server/pkcs11_key_store.h"
#include "tpm_manager/client/tpm_nvram_dbus_proxy.h"
#include "tpm_manager/client/tpm_ownership_dbus_proxy.h"
#include "tpm_manager/common/tpm_nvram_interface.h"
#include "tpm_manager/common/tpm_ownership_interface.h"

namespace attestation {

// An implementation of AttestationInterface for the core attestation service.
// Access to TPM, network and local file-system resources occurs asynchronously
// with the exception of Initialize(). All methods must be called on the same
// thread that originally called Initialize().
// Usage:
//   std::unique_ptr<AttestationInterface> attestation =
//       new AttestationService();
//   CHECK(attestation->Initialize());
//   attestation->CreateGoogleAttestedKey(...);
//
// THREADING NOTES:
// This class runs a worker thread and delegates all calls to it. This keeps the
// public methods non-blocking while allowing complex implementation details
// with dependencies on the TPM, network, and filesystem to be coded in a more
// readable way. It also serves to serialize method execution which reduces
// complexity with TPM state.
//
// Tasks that run on the worker thread are bound with base::Unretained which is
// safe because the thread is owned by this class (so it is guaranteed not to
// process a task after destruction). Weak pointers are used to post replies
// back to the main thread.
class AttestationService : public AttestationInterface {
 public:
  AttestationService();
  ~AttestationService() override = default;

  // AttestationInterface methods.
  bool Initialize() override;
  void CreateGoogleAttestedKey(
      const CreateGoogleAttestedKeyRequest& request,
      const CreateGoogleAttestedKeyCallback& callback) override;
  void GetKeyInfo(const GetKeyInfoRequest& request,
                  const GetKeyInfoCallback& callback) override;
  void GetEndorsementInfo(const GetEndorsementInfoRequest& request,
                          const GetEndorsementInfoCallback& callback) override;
  void GetAttestationKeyInfo(
      const GetAttestationKeyInfoRequest& request,
      const GetAttestationKeyInfoCallback& callback) override;
  void ActivateAttestationKey(
      const ActivateAttestationKeyRequest& request,
      const ActivateAttestationKeyCallback& callback) override;
  void CreateCertifiableKey(
      const CreateCertifiableKeyRequest& request,
      const CreateCertifiableKeyCallback& callback) override;
  void Decrypt(const DecryptRequest& request,
               const DecryptCallback& callback) override;
  void Sign(const SignRequest& request, const SignCallback& callback) override;
  void RegisterKeyWithChapsToken(
      const RegisterKeyWithChapsTokenRequest& request,
      const RegisterKeyWithChapsTokenCallback& callback) override;
  void GetStatus(
      const GetStatusRequest& request,
      const GetStatusCallback& callback) override;
  void Verify(
      const VerifyRequest& request,
      const VerifyCallback& callback) override;
  void CreateEnrollRequest(
      const CreateEnrollRequestRequest& request,
      const CreateEnrollRequestCallback& callback) override;
  void FinishEnroll(
      const FinishEnrollRequest& request,
      const FinishEnrollCallback& callback) override;
  void CreateCertificateRequest(
      const CreateCertificateRequestRequest& request,
      const CreateCertificateRequestCallback& callback) override;
  void FinishCertificateRequest(
      const FinishCertificateRequestRequest& request,
      const FinishCertificateRequestCallback& callback) override;
  void SignEnterpriseChallenge(
      const SignEnterpriseChallengeRequest& request,
      const SignEnterpriseChallengeCallback& callback) override;
  void SignSimpleChallenge(
      const SignSimpleChallengeRequest& request,
      const SignSimpleChallengeCallback& callback) override;
  void SetKeyPayload(
      const SetKeyPayloadRequest& request,
      const SetKeyPayloadCallback& callback) override;
  void DeleteKeys(
      const DeleteKeysRequest& request,
      const DeleteKeysCallback& callback) override;
  void ResetIdentity(
      const ResetIdentityRequest& request,
      const ResetIdentityCallback& callback) override;
  void SetSystemSalt(
      const SetSystemSaltRequest& request,
      const SetSystemSaltCallback& callback) override;

  // Mutators useful for testing.
  void set_crypto_utility(CryptoUtility* crypto_utility) {
    crypto_utility_ = crypto_utility;
  }

  void set_database(Database* database) { database_ = database; }

  void set_http_transport(
      const std::shared_ptr<brillo::http::Transport>& transport) {
    http_transport_ = transport;
  }

  void set_key_store(KeyStore* key_store) { key_store_ = key_store; }

  void set_tpm_utility(TpmUtility* tpm_utility) { tpm_utility_ = tpm_utility; }

  void set_hwid(const std::string& hwid) { hwid_ = hwid; }

  // So tests don't need to duplicate URL decisions.
  const std::string& attestation_ca_origin() { return attestation_ca_origin_; }

 private:
  friend class AttestationServiceTest;

  typedef std::map<std::string, std::string> CertRequestMap;
  enum ACARequestType {
    kEnroll,          // Enrolls a device, certifying an identity key.
    kGetCertificate,  // Issues a certificate for a TPM-backed key.
  };

  // Attestation service worker thread class that cleans up after stopping.
  class ServiceWorkerThread : public base::Thread {
   public:
    ServiceWorkerThread(AttestationService* service)
        : base::Thread("Attestation Service Worker"), service_(service) {
      DCHECK(service_);
    }
    ~ServiceWorkerThread() override {
      Stop();
    }

   private:
    void CleanUp() override {
      service_->ShutdownTask();
    }

    AttestationService* const service_;

    DISALLOW_COPY_AND_ASSIGN(ServiceWorkerThread);
  };

  // A relay callback which allows the use of weak pointer semantics for a reply
  // to TaskRunner::PostTaskAndReply.
  template <typename ReplyProtobufType>
  void TaskRelayCallback(
      const base::Callback<void(const ReplyProtobufType&)> callback,
      const std::shared_ptr<ReplyProtobufType>& reply) {
    callback.Run(*reply);
  }

  // Initialization to be run on the worker thread.
  void InitializeTask();

  // Shutdown to be run on the worker thread.
  void ShutdownTask();

  // A blocking implementation of CreateGoogleAttestedKey appropriate to run on
  // the worker thread.
  void CreateGoogleAttestedKeyTask(
      const CreateGoogleAttestedKeyRequest& request,
      const std::shared_ptr<CreateGoogleAttestedKeyReply>& result);

  // A blocking implementation of GetKeyInfo.
  void GetKeyInfoTask(const GetKeyInfoRequest& request,
                      const std::shared_ptr<GetKeyInfoReply>& result);

  // A blocking implementation of GetEndorsementInfo.
  void GetEndorsementInfoTask(
      const GetEndorsementInfoRequest& request,
      const std::shared_ptr<GetEndorsementInfoReply>& result);

  // A blocking implementation of GetAttestationKeyInfo.
  void GetAttestationKeyInfoTask(
      const GetAttestationKeyInfoRequest& request,
      const std::shared_ptr<GetAttestationKeyInfoReply>& result);

  // A blocking implementation of ActivateAttestationKey.
  void ActivateAttestationKeyTask(
      const ActivateAttestationKeyRequest& request,
      const std::shared_ptr<ActivateAttestationKeyReply>& result);

  // A blocking implementation of CreateCertifiableKey.
  void CreateCertifiableKeyTask(
      const CreateCertifiableKeyRequest& request,
      const std::shared_ptr<CreateCertifiableKeyReply>& result);

  // A blocking implementation of Decrypt.
  void DecryptTask(const DecryptRequest& request,
                   const std::shared_ptr<DecryptReply>& result);

  // A blocking implementation of Sign.
  void SignTask(const SignRequest& request,
                const std::shared_ptr<SignReply>& result);

  // A synchronous implementation of RegisterKeyWithChapsToken.
  void RegisterKeyWithChapsTokenTask(
      const RegisterKeyWithChapsTokenRequest& request,
      const std::shared_ptr<RegisterKeyWithChapsTokenReply>& result);

  // A synchronous implementation of GetStatus.
  void GetStatusTask(
      const GetStatusRequest& request,
      const std::shared_ptr<GetStatusReply>& result);

  // A synchronous implementation of Verify.
  void VerifyTask(
      const VerifyRequest& request,
      const std::shared_ptr<VerifyReply>& result);

  // A synchronous implementation of CreateEnrollRequest.
  void CreateEnrollRequestTask(
      const CreateEnrollRequestRequest& request,
      const std::shared_ptr<CreateEnrollRequestReply>& result);

  // A synchronous implementation of FinishEnroll.
  void FinishEnrollTask(
      const FinishEnrollRequest& request,
      const std::shared_ptr<FinishEnrollReply>& result);

  // A synchronous implementation of CreateCertificateRequest.
  void CreateCertificateRequestTask(
      const CreateCertificateRequestRequest& request,
      const std::shared_ptr<CreateCertificateRequestReply>& result);

  // A synchronous implementation of FinishCertificateRequest.
  void FinishCertificateRequestTask(
      const FinishCertificateRequestRequest& request,
      const std::shared_ptr<FinishCertificateRequestReply>& result);

  // A synchronous implementation of SignEnterpriseChallenge.
  void SignEnterpriseChallengeTask(
      const SignEnterpriseChallengeRequest& request,
      const std::shared_ptr<SignEnterpriseChallengeReply>& result);

  // A synchronous implementation of SignSimpleChallenge.
  void SignSimpleChallengeTask(
      const SignSimpleChallengeRequest& request,
      const std::shared_ptr<SignSimpleChallengeReply>& result);

  // A synchronous implementation of SetKeyPayload.
  void SetKeyPayloadTask(
      const SetKeyPayloadRequest& request,
      const std::shared_ptr<SetKeyPayloadReply>& result);

  // A synchronous implementation of DeleteKeys.
  void DeleteKeysTask(
      const DeleteKeysRequest& request,
      const std::shared_ptr<DeleteKeysReply>& result);

  // A synchronous implementation of ResetIdentity.
  void ResetIdentityTask(
      const ResetIdentityRequest& request,
      const std::shared_ptr<ResetIdentityReply>& result);

  // Returns true iff all information required for enrollment with the Google
  // Attestation CA is available.
  bool IsPreparedForEnrollment();

  // Returns true iff enrollment with the Google Attestation CA has been
  // completed.
  bool IsEnrolled();

  // Creates an enrollment request compatible with the Google Attestation CA.
  // Returns true on success.
  bool CreateEnrollRequestInternal(std::string* enroll_request);

  // Finishes enrollment given an |enroll_response| from the Google Attestation
  // CA. Returns true on success. On failure, returns false and sets
  // |server_error| to the error string from the CA.
  bool FinishEnrollInternal(const std::string& enroll_response,
                            std::string* server_error);

  // Creates a |certificate_request| compatible with the Google Attestation CA
  // for the given |key|, according to the given |profile|, |username| and
  // |origin|.
  bool CreateCertificateRequestInternal(const std::string& username,
                                        const CertifiedKey& key,
                                        CertificateProfile profile,
                                        const std::string& origin,
                                        std::string* certificate_request,
                                        std::string* message_id);

  // Finishes a certificate request by decoding the |certificate_response| to
  // recover the |certificate_chain| and storing it in association with the
  // |key| identified by |username| and |key_label|. Returns true on success. On
  // failure, returns false and sets |server_error| to the error string from the
  // CA. Calls PopulateAndStoreCertifiedKey internally.
  bool FinishCertificateRequestInternal(const std::string& certificate_response,
                                        const std::string& username,
                                        const std::string& key_label,
                                        const std::string& message_id,
                                        CertifiedKey* key,
                                        std::string* certificate_chain,
                                        std::string* server_error);

  // Recover the |certificate_chain| from |response_pb| and store it in
  // association with the |key| identified by |username| and |key_label|.
  // Returns true on success.
  bool PopulateAndStoreCertifiedKey(
    const AttestationCertificateResponse& response_pb,
    const std::string& username,
    const std::string& key_label,
    CertifiedKey* key,
    std::string* certificate_chain);

  // Sends a |request_type| |request| to the Google Attestation CA and waits for
  // the |reply|. Returns true on success.
  bool SendACARequestAndBlock(ACARequestType request_type,
                              const std::string& request,
                              std::string* reply);

  // Creates, certifies, and saves a new |key| for |username| with the given
  // |key_label|, |key_type|, and |key_usage|. Returns true on success.
  bool CreateKey(const std::string& username,
                 const std::string& key_label,
                 KeyType key_type,
                 KeyUsage key_usage,
                 CertifiedKey* key);

  // Finds the |key| associated with |username| and |key_label|. Returns false
  // if such a key does not exist.
  bool FindKeyByLabel(const std::string& username,
                      const std::string& key_label,
                      CertifiedKey* key);

  // Saves the |key| associated with |username| and |key_label|. Returns true on
  // success.
  bool SaveKey(const std::string& username,
               const std::string& key_label,
               const CertifiedKey& key);

  // Deletes the key associated with |username| and |key_label|.
  void DeleteKey(const std::string& username, const std::string& key_label);

  // Deletes the key associated with |username| and having prefix |key_prefix|.
  bool DeleteKeysByPrefix(const std::string& username,
                          const std::string& key_prefix);

  // Adds named device-wide key to the attestation database.
  bool AddDeviceKey(const std::string& key_label, const CertifiedKey& key);

  // Removes a device-wide key from the attestation database.
  void RemoveDeviceKey(const std::string& key_label);

  // Removes device-wide keys with a given prefix from the attestation
  // database.
  bool RemoveDeviceKeysByPrefix(const std::string& key_prefix);

  // Creates a PEM certificate chain from the credential fields of a |key|.
  std::string CreatePEMCertificateChain(const CertifiedKey& key);

  // Creates a certificate in PEM format from a DER encoded X.509 certificate.
  std::string CreatePEMCertificate(const std::string& certificate);

  // Chooses a temporal index which will be used by the ACA to create a
  // certificate.  This decision factors in the currently signed-in |user| and
  // the |origin| of the certificate request.  The strategy is to find an index
  // which has not already been used by another user for the same origin.
  int ChooseTemporalIndex(const std::string& user, const std::string& origin);

  // Creates a Google Attestation CA URL for the given |request_type|.
  std::string GetACAURL(ACARequestType request_type) const;

  // Creates a X.509/DER SubjectPublicKeyInfo for the given |key_type| and
  // |public_key|. On success returns true and provides |public_key_info|.
  bool GetSubjectPublicKeyInfo(KeyType key_type,
                               const std::string& public_key,
                               std::string* public_key_info) const;

  // Prepares the attestation system for enrollment with an ACA.
  void PrepareForEnrollment();

  // Activates an attestation key given an |encrypted_certificate|. On success
  // returns true and provides the decrypted |certificate| if not null. If
  // |save_certificate| is set, also writes the |certificate| to the database.
  bool ActivateAttestationKeyInternal(const EncryptedIdentityCredential&
    encrypted_certificate, bool save_certificate, std::string* certificate);

  // Checks if PCR0 indicates that the system booted in verified mode.
  // Always reads PCR0 contents from TPM, so works even when not prepared
  // for enrollment.
  bool IsVerifiedMode() const;

  // Validates incoming enterprise challenge data.
  bool ValidateEnterpriseChallenge(const SignedData& signed_challenge);

  // Encrypts a KeyInfo protobuf as required for an enterprise challenge
  // response.
  bool EncryptEnterpriseKeyInfo(const KeyInfo& key_info,
                                EncryptedData* encrypted_data);

  // Signs data using the provided key. On success, returns true and fills
  // |response| with serialized SignedData.
  bool SignChallengeData(const CertifiedKey& key,
                         const std::string& data_to_sign,
                         std::string* response);

  // Verifies identity key binding data.
  bool VerifyIdentityBinding(const IdentityBinding& binding);

  // Computes and returns the PCR value for a known 3-byte |mode|:
  //  - byte 0: 1 if in developer mode, 0 otherwise,
  //  - byte 1: 1 if in recovery mode, 0 otherwise,
  //  - byte 2: 1 if verified firmware, 0 if developer firmware.
  std::string GetPCRValueForMode(const char* mode) const;

  // Verifies that PCR quote signature is correct.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  bool VerifyQuoteSignature(const std::string& aik_public_key_info,
                            const Quote& quote,
                            int pcr_index);

  // Verifies PCR0 quote.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  bool VerifyPCR0Quote(const std::string& aik_public_key_info,
                       const Quote& pcr0_quote);

  // Verifies PCR1 quote.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  bool VerifyPCR1Quote(const std::string& aik_public_key_info,
                       const Quote& pcr1_quote);

  // Calculates the digest for a certified key. The digest is TPM version (1.2
  // vs 2.0) specific.
  // public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  // public_key_tpm_format must be provided in TPM format.
  bool GetCertifiedKeyDigest(const std::string& public_key_info,
                             const std::string& public_key_tpm_format,
                             std::string* key_digest);

  // Verifies a certified key.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  // public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  // key_info is a TPM_CERTIFY_INFO structure.
  bool VerifyCertifiedKey(const std::string& aik_public_key_info,
                          const std::string& public_key_info,
                          const std::string& public_key_tpm_format,
                          const std::string& key_info,
                          const std::string& proof);

  // Creates a certified key and verifies it.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  bool VerifyCertifiedKeyGeneration(const std::string& aik_key_blob,
                                    const std::string& aik_public_key_info);

  // Performs AIK activation with a fake credential.
  // ek_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  bool VerifyActivateIdentity(const std::string& ek_public_key_info,
                              const std::string& aik_public_key_tpm_format);

  base::WeakPtr<AttestationService> GetWeakPtr();

  const std::string attestation_ca_origin_;

  // Other than initialization and destruction, these are used only by the
  // worker thread.
  CryptoUtility* crypto_utility_{nullptr};
  Database* database_{nullptr};
  std::shared_ptr<brillo::http::Transport> http_transport_;
  KeyStore* key_store_{nullptr};
  // |tpm_utility_| typically points to |default_tpm_utility_| created/destroyed
  // on the |worker_thread_|. As such, should not be accessed after that thread
  // is stopped/destroyed.
  TpmUtility* tpm_utility_{nullptr};
  std::string hwid_;
  CertRequestMap pending_cert_requests_;
  std::string system_salt_;

  // Default implementations for the above interfaces. These will be setup
  // during Initialize() if the corresponding interface has not been set with a
  // mutator.
  std::unique_ptr<CryptoUtilityImpl> default_crypto_utility_;
  std::unique_ptr<DatabaseImpl> default_database_;
  std::unique_ptr<Pkcs11KeyStore> default_key_store_;
  std::unique_ptr<chaps::TokenManagerClient> pkcs11_token_manager_;
  // |default_tpm_utility_| is created and destroyed on the |worker_thread_|,
  // and is not available after the thread is stopped/destroyed.
  std::unique_ptr<TpmUtility> default_tpm_utility_;

  // All work is done in the background. This serves to serialize requests and
  // allow synchronous implementation of complex methods. This is intentionally
  // declared after the thread-owned members.
  std::unique_ptr<ServiceWorkerThread> worker_thread_;

  // Declared last so any weak pointers are destroyed first.
  base::WeakPtrFactory<AttestationService> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(AttestationService);
};

}  // namespace attestation

#endif  // ATTESTATION_SERVER_ATTESTATION_SERVICE_H_
