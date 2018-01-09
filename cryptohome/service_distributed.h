// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SERVICE_DISTRIBUTED_H_
#define CRYPTOHOME_SERVICE_DISTRIBUTED_H_

#include <memory>
#include <string>

#include "attestation/common/attestation_interface.h"
#include "cryptohome/service.h"

namespace cryptohome {

// ServiceDistributed
// Represents a Service where attestation functionality is implemented
// in a separated attestationd daemon.
class ServiceDistributed : public Service {
 public:
  ServiceDistributed();
  virtual ~ServiceDistributed();

  void AttestationInitialize() override;
  void AttestationInitializeTpm() override;
  void AttestationInitializeTpmComplete() override;
  void AttestationGetTpmStatus(GetTpmStatusReply* reply) override;
  bool AttestationGetDelegateCredentials(
      brillo::SecureBlob* blob,
      brillo::SecureBlob* secret,
      bool* has_reset_lock_permissions) override;

  gboolean TpmIsAttestationPrepared(gboolean* OUT_prepared,
                                    GError** error) override;
  gboolean TpmVerifyAttestationData(gboolean is_cros_core,
                                    gboolean* OUT_verified,
                                    GError** error) override;
  gboolean TpmVerifyEK(gboolean is_cros_core,
                       gboolean* OUT_verified,
                       GError** error) override;
  gboolean TpmAttestationCreateEnrollRequest(gint pca_type,
                                             GArray** OUT_pca_request,
                                             GError** error) override;
  gboolean AsyncTpmAttestationCreateEnrollRequest(gint pca_type,
                                                  gint* OUT_async_id,
                                                  GError** error) override;
  gboolean TpmAttestationEnroll(gint pca_type,
                                GArray* pca_response,
                                gboolean* OUT_success,
                                GError** error) override;
  gboolean AsyncTpmAttestationEnroll(gint pca_type,
                                     GArray* pca_response,
                                     gint* OUT_async_id,
                                     GError** error) override;
  gboolean TpmAttestationCreateCertRequest(gint pca_type,
                                           gint certificate_profile,
                                           gchar* username,
                                           gchar* request_origin,
                                           GArray** OUT_pca_request,
                                           GError** error) override;
  gboolean AsyncTpmAttestationCreateCertRequest(gint pca_type,
                                                gint certificate_profile,
                                                gchar* username,
                                                gchar* request_origin,
                                                gint* OUT_async_id,
                                                GError** error) override;
  gboolean TpmAttestationFinishCertRequest(GArray* pca_response,
                                           gboolean is_user_specific,
                                           gchar* username,
                                           gchar* key_name,
                                           GArray** OUT_cert,
                                           gboolean* OUT_success,
                                           GError** error) override;
  gboolean AsyncTpmAttestationFinishCertRequest(GArray* pca_response,
                                                gboolean is_user_specific,
                                                gchar* username,
                                                gchar* key_name,
                                                gint* OUT_async_id,
                                                GError** error) override;
  gboolean TpmIsAttestationEnrolled(gboolean* OUT_is_enrolled,
                                    GError** error) override;
  gboolean TpmAttestationDoesKeyExist(gboolean is_user_specific,
                                      gchar* username,
                                      gchar* key_name,
                                      gboolean* OUT_exists,
                                      GError** error) override;
  gboolean TpmAttestationGetCertificate(gboolean is_user_specific,
                                        gchar* username,
                                        gchar* key_name,
                                        GArray** OUT_certificate,
                                        gboolean* OUT_success,
                                        GError** error) override;
  gboolean TpmAttestationGetPublicKey(gboolean is_user_specific,
                                      gchar* username,
                                      gchar* key_name,
                                      GArray** OUT_public_key,
                                      gboolean* OUT_success,
                                      GError** error) override;
  gboolean TpmAttestationRegisterKey(gboolean is_user_specific,
                                     gchar* username,
                                     gchar* key_name,
                                     gint* OUT_async_id,
                                     GError** error) override;
  gboolean TpmAttestationSignEnterpriseChallenge(
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gchar* domain,
      GArray* device_id,
      gboolean include_signed_public_key,
      GArray* challenge,
      gint* OUT_async_id,
      GError** error) override;
  gboolean TpmAttestationSignEnterpriseVaChallenge(
      gint va_type,
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gchar* domain,
      GArray* device_id,
      gboolean include_signed_public_key,
      GArray* challenge,
      gint* OUT_async_id,
      GError** error) override;
  gboolean TpmAttestationSignSimpleChallenge(gboolean is_user_specific,
                                             gchar* username,
                                             gchar* key_name,
                                             GArray* challenge,
                                             gint* OUT_async_id,
                                             GError** error) override;
  gboolean TpmAttestationGetKeyPayload(gboolean is_user_specific,
                                       gchar* username,
                                       gchar* key_name,
                                       GArray** OUT_payload,
                                       gboolean* OUT_success,
                                       GError** error) override;
  gboolean TpmAttestationSetKeyPayload(gboolean is_user_specific,
                                       gchar* username,
                                       gchar* key_name,
                                       GArray* payload,
                                       gboolean* OUT_success,
                                       GError** error) override;
  gboolean TpmAttestationDeleteKeys(gboolean is_user_specific,
                                    gchar* username,
                                    gchar* key_prefix,
                                    gboolean* OUT_success,
                                    GError** error) override;
  gboolean TpmAttestationGetEK(gchar** ek_info,
                               gboolean* OUT_success,
                               GError** error) override;
  gboolean TpmAttestationResetIdentity(gchar* reset_token,
                                       GArray** OUT_reset_request,
                                       gboolean* OUT_success,
                                       GError** error) override;
  gboolean GetEndorsementInfo(const GArray* request,
                              DBusGMethodInvocation* context) override;
  gboolean InitializeCastKey(const GArray* request,
                             DBusGMethodInvocation* context) override;
  gboolean TpmAttestationGetEnrollmentId(GArray** OUT_enrollment_id,
                                             gboolean* OUT_success,
                                             GError** error) override;

 private:
  // A helper function which maps an integer to a valid CertificateProfile.
  static attestation::CertificateProfile GetProfile(int profile_value);

  // Send GetKeyInfoRequest to attestationd and wait for reply.
  bool GetKeyInfo(gboolean is_user_specific,
                  gchar* username,
                  gchar* key_name,
                  attestation::GetKeyInfoReply* key_info);

  // Prepare interface to attestationd, if not prepared yet.
  // Can be called multiple times.
  // Starts attestation_thread_ and initializes interface.
  bool PrepareInterface();

  // Get a unique async_id for use in async dbus methods.
  static int AllocateAsyncId();

  // Post a method on the attestation_thread_.
  template <typename MethodType>
  bool Post(const MethodType& method);

  // Post a method on the attestation_thread and wait for its completion.
  template <typename MethodType>
  bool PostAndWait(const MethodType& method);

  // Send request to attestationd and wait for reply.
  // Request is sent from attestation_thread_.
  template <typename ReplyProtoType, typename MethodType>
  bool SendRequestAndWait(const MethodType& method,
                          ReplyProtoType* reply_proto);

  // A helper function which maps an integer to a valid ACAType.
  static gboolean ConvertPCATypeToACAType(gint pca_type,
                                          attestation::ACAType* aca_type,
                                          GError** error);
  // A helper function which maps an integer to a valid VAType.
  static gboolean ConvertToVAType(gint type,
                                  attestation::VAType* va_type,
                                  GError** error);

  // Helper methods that fill GError where an error
  // is returned directly from the original handler.
  static void ReportErrorFromStatus(GError** error,
                                    attestation::AttestationStatus status);
  static void ReportSendFailure(GError** error);
  static void ReportUnsupportedPCAType(GError** error, int pca_type);
  static void ReportUnsupportedVAType(GError** error, int type);

  std::unique_ptr<attestation::AttestationInterface>
      default_attestation_interface_;
  attestation::AttestationInterface* attestation_interface_;

  // Callbacks processing different replies.
  // Called from attestation_thread_.

  // Process replies that contain only status.
  // Sends event with proper async_id.
  template <typename ReplyProtoType>
  void ProcessStatusReply(int async_id, const ReplyProtoType& reply);

  // Process replies that contain status and some binary data.
  // The binary data is retrieved from the reply using func().
  // Sends event with proper async_id.
  template <typename ReplyProtoType>
  void ProcessDataReply(const std::string& (ReplyProtoType::*func)() const,
                        int async_id,
                        const ReplyProtoType& reply);

  // Process GetEndorsementInfoReply.
  void ProcessGetEndorsementInfoReply(
      DBusGMethodInvocation* context,
      const attestation::GetEndorsementInfoReply& reply);

  // Tasks executed by attestation_thread_ to process
  // async requests.
  void DoGetEndorsementInfo(const brillo::SecureBlob& request_array,
                            DBusGMethodInvocation* context);
  void DoInitializeCastKey(const brillo::SecureBlob& request_array,
                           DBusGMethodInvocation* context);

  // Does PCR0 contain the value that indicates the verified mode.
  bool IsVerifiedModeMeasured();

  base::WeakPtr<ServiceDistributed> GetWeakPtr();

  // Message loop thread servicing dbus communications with attestationd.
  base::Thread attestation_thread_{"attestation_thread"};

  // Declared last, so that weak pointers are destroyed first.
  base::WeakPtrFactory<ServiceDistributed> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ServiceDistributed);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SERVICE_DISTRIBUTED_H_
