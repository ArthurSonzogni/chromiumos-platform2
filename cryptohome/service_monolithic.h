// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SERVICE_MONOLITHIC_H_
#define CRYPTOHOME_SERVICE_MONOLITHIC_H_

#include <memory>
#include <string>

#include "cryptohome/attestation.h"
#include "cryptohome/service.h"

namespace cryptohome {

// ServiceMonolithic
// Represents a Service where attestation functionality is implemented
// inside cryptohome.
class ServiceMonolithic : public Service {
 public:
  explicit ServiceMonolithic(const std::string& abe_data);
  virtual ~ServiceMonolithic();

  virtual void set_attestation(Attestation* attestation) {
    attestation_ = attestation;
  }

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
  gboolean TpmAttestationCreateCertRequest(
      gint pca_type,
      gint certificate_profile,
      gchar* username,
      gchar* request_origin,
      GArray** OUT_pca_request,
      GError** error) override;
  gboolean AsyncTpmAttestationCreateCertRequest(
      gint pca_type,
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
  gboolean AsyncTpmAttestationFinishCertRequest(
      GArray* pca_response,
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
                                              gboolean *OUT_exists,
                                              GError** error) override;
  gboolean TpmAttestationGetCertificate(gboolean is_user_specific,
                                                gchar* username,
                                                gchar* key_name,
                                                GArray **OUT_certificate,
                                                gboolean* OUT_success,
                                                GError** error) override;
  gboolean TpmAttestationGetPublicKey(gboolean is_user_specific,
                                              gchar* username,
                                              gchar* key_name,
                                              GArray **OUT_public_key,
                                              gboolean* OUT_success,
                                              GError** error) override;
  gboolean TpmAttestationRegisterKey(gboolean is_user_specific,
                                             gchar* username,
                                             gchar* key_name,
                                             gint *OUT_async_id,
                                             GError** error) override;
  gboolean TpmAttestationSignEnterpriseChallenge(
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      gchar* domain,
      GArray* device_id,
      gboolean include_signed_public_key,
      GArray* challenge,
      gint *OUT_async_id,
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
      gint *OUT_async_id,
      GError** error) override;
  gboolean TpmAttestationSignSimpleChallenge(
      gboolean is_user_specific,
      gchar* username,
      gchar* key_name,
      GArray* challenge,
      gint *OUT_async_id,
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
  // Runs on the mount thread.
  virtual void DoGetEndorsementInfo(const brillo::SecureBlob& request,
                                    DBusGMethodInvocation* context);
  gboolean GetEndorsementInfo(const GArray* request,
                              DBusGMethodInvocation* context) override;
  // Runs on the mount thread.
  virtual void DoInitializeCastKey(const brillo::SecureBlob& request,
                                   DBusGMethodInvocation* context);
  gboolean InitializeCastKey(const GArray* request,
                             DBusGMethodInvocation* context) override;
  gboolean TpmAttestationGetEnrollmentId(GArray** OUT_enrollment_id,
                                         gboolean* OUT_success,
                                         GError** error) override;

 private:
  // Parses |data| and fills in |abe_data|, or clears it if |data| are invalid.
  static bool GetAttestationBasedEnterpriseEnrollmentData(
      const std::string& data, brillo::SecureBlob* abe_data);

  FRIEND_TEST_ALL_PREFIXES(ServiceTest, ValidAbeDataTest);
  FRIEND_TEST_ALL_PREFIXES(ServiceTest, InvalidAbeDataTest);

  std::unique_ptr<Attestation> default_attestation_;
  Attestation* attestation_;

  brillo::SecureBlob abe_data_;

  DISALLOW_COPY_AND_ASSIGN(ServiceMonolithic);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SERVICE_MONOLITHIC_H_
