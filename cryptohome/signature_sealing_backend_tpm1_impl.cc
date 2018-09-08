// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/signature_sealing_backend_tpm1_impl.h"

#include <stdint.h>

#include <algorithm>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/memory/free_deleter.h>
#include <base/strings/stringprintf.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha) - needs tss.h

#include "cryptohome/cryptolib.h"
#include "cryptohome/tpm_impl.h"
#include "signature_sealed_data.pb.h"  // NOLINT(build/include)

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::CombineBlobs;
using brillo::SecureBlob;
using trousers::ScopedTssContext;
using trousers::ScopedTssKey;
using trousers::ScopedTssMemory;
using trousers::ScopedTssObject;
using trousers::ScopedTssPolicy;

namespace cryptohome {

namespace {

// Size of the migration destination key to be generated. Note that the choice
// of this size is constrained by restrictions from the TPM 1.2 specs.
constexpr int kMigrationDestinationKeySizeBits = 2048;
constexpr int kMigrationDestinationKeySizeBytes =
    kMigrationDestinationKeySizeBits / 8;
constexpr int kMigrationDestinationKeySizeFlag = TSS_KEY_SIZE_2048;

// Size of the certified migratable key to be created. Note that the choice of
// this size is dictated by restrictions from the TPM 1.2 specs.
constexpr int kCmkKeySizeBits = 2048;
constexpr int kCmkKeySizeBytes = kCmkKeySizeBits / 8;
constexpr int kCmkPrivateKeySizeBytes = kCmkKeySizeBytes / 2;
constexpr int kCmkKeySizeFlag = TSS_KEY_SIZE_2048;

// The RSA OAEP label parameter specified to be used by the TPM 1.2 specs (see
// TPM 1.2 Part 1 Section 31.1.1 "TPM_ES_RSAESOAEP_SHA1_MGF1").
constexpr char kTpmRsaOaepLabel[] = {'T', 'C', 'P', 'A'};

// Sizes of the two parts of the migrated CMK private key blob: as described in
// TPM 1.2 Part 3 Section 11.9 ("TPM_CMK_CreateBlob"), one part goes into the
// OAEP seed and the rest goes into the TPM_MIGRATE_ASYMKEY struct.
constexpr int kMigratedCmkPrivateKeySeedPartSizeBytes = 16;
constexpr int kMigratedCmkPrivateKeyRestPartSizeBytes = 112;
static_assert(kMigratedCmkPrivateKeySeedPartSizeBytes == SHA_DIGEST_LENGTH - 4,
              "Invalid private key seed part size constant");
static_assert(kMigratedCmkPrivateKeySeedPartSizeBytes +
                      kMigratedCmkPrivateKeyRestPartSizeBytes ==
                  kCmkPrivateKeySizeBytes,
              "Invalid private key part size constants");

// Size of the TPM_MIGRATE_ASYMKEY structure containing the part of the migrated
// private key blob.
constexpr int kTpmMigrateAsymkeyBlobSize =
    sizeof(TPM_PAYLOAD_TYPE) /* for payload */ +
    SHA_DIGEST_LENGTH /* for usageAuth.authdata */ +
    SHA_DIGEST_LENGTH /* for pubDataDigest.digest */ +
    sizeof(UINT32) /* for partPrivKeyLen */ +
    kMigratedCmkPrivateKeyRestPartSizeBytes /* for *partPrivKey */;

// Scoped wrapper of the TPM_KEY12 struct.
class ScopedKey12 final {
 public:
  ScopedKey12() { memset(&value_, 0, sizeof(TPM_KEY12)); }

  ~ScopedKey12() {
    free(value_.algorithmParms.parms);
    free(value_.pubKey.key);
    free(value_.encData);
    free(value_.PCRInfo);
  }

  const TPM_KEY12& operator*() const { return value_; }
  const TPM_KEY12* operator->() const { return &value_; }
  TPM_KEY12* ptr() { return &value_; }

 private:
  TPM_KEY12 value_;

  DISALLOW_COPY_AND_ASSIGN(ScopedKey12);
};

class UnsealingSessionTpm1Impl final
    : public SignatureSealingBackend::UnsealingSession {
 public:
  UnsealingSessionTpm1Impl(TpmImpl* tpm,
                           const Blob& srk_wrapped_cmk,
                           const Blob& public_key_spki_der,
                           const Blob& delegate_blob,
                           const Blob& delegate_secret,
                           const Blob& cmk_pubkey,
                           const Blob& protection_key_pubkey,
                           crypto::ScopedRSA migration_destination_rsa,
                           const Blob& migration_destination_key_pubkey);
  ~UnsealingSessionTpm1Impl() override;

  // UnsealingSession:
  Algorithm GetChallengeAlgorithm() override;
  Blob GetChallengeValue() override;
  bool Unseal(const Blob& signed_challenge_value,
              SecureBlob* unsealed_value) override;

 private:
  // Unowned.
  TpmImpl* const tpm_;
  // The blob of the CMK wrapped by the SRK.
  const Blob srk_wrapped_cmk_;
  // The DER-encoded Subject Public Key Info of the protection key.
  const Blob public_key_spki_der_;
  // The blob for the owner delegation.
  const Blob delegate_blob_;
  // The delegate secret for the delegate blob.
  const Blob delegate_secret_;
  // The TPM_PUBKEY blob of the CMK.
  const Blob cmk_pubkey_;
  // The SHA-1 digest of |cmk_pubkey_|.
  const Blob cmk_pubkey_digest_;
  // The TPM_PUBKEY blob of the protection key.
  const Blob protection_key_pubkey_;
  // The SHA-1 digest of |protection_key_pubkey_|.
  const Blob protection_key_pubkey_digest_;
  // The private RSA key of the migration destination key.
  const crypto::ScopedRSA migration_destination_rsa_;
  // The TPM_PUBKEY blob of the migration destination key.
  const Blob migration_destination_key_pubkey_;
  // The SHA-1 digest of |migration_destination_key_pubkey_|.
  const Blob migration_destination_key_pubkey_digest_;
  // The SHA-1 digest of the TPM_MSA_COMPOSITE structure containing a sole
  // reference to |protection_key_pubkey_digest_|.
  const Blob msa_composite_digest_;

  DISALLOW_COPY_AND_ASSIGN(UnsealingSessionTpm1Impl);
};

std::string FormatTrousersErrorCode(TSS_RESULT result) {
  return base::StringPrintf("TPM error 0x%x (%s): ", result,
                            Trspi_Error_String(result));
}

// Extracts the public modulus from the OpenSSL RSA struct.
bool GetRsaModulus(const RSA& rsa, Blob* modulus) {
  modulus->resize(BN_num_bytes(rsa.n));
  if (BN_bn2bin(rsa.n, modulus->data()) != modulus->size()) {
    LOG(ERROR) << "Failed to extract RSA modulus: size mismatch";
    return false;
  }
  return true;
}

// Parses the public key that is protecting the sealed data. The key size in
// bits via |key_size_bits|, and the RSA key public modulus is returned via
// |key_modulus|.
bool ParseProtectionKeySpki(const Blob& public_key_spki_der,
                            int* key_size_bits,
                            Blob* key_modulus) {
  const unsigned char* asn1_ptr = public_key_spki_der.data();
  crypto::ScopedEVP_PKEY pkey(
      d2i_PUBKEY(nullptr, &asn1_ptr, public_key_spki_der.size()));
  if (!pkey) {
    LOG(ERROR) << "Error parsing protection public key: Failed to parse "
                  "Subject Public Key Info DER";
    return false;
  }
  crypto::ScopedRSA rsa(EVP_PKEY_get1_RSA(pkey.get()));
  if (!rsa) {
    LOG(ERROR) << "Error parsing protection public key: Non-RSA key";
    return false;
  }
  const BN_ULONG key_exponent_word = BN_get_word(rsa->e);
  if (key_exponent_word != kWellKnownExponent) {
    // Trousers only supports the well-known exponent, failing internally on
    // incorrect data serialization when other exponents are used.
    LOG(ERROR) << "Error parsing protection public key: Exponent must be "
               << kWellKnownExponent;
    return false;
  }
  *key_size_bits = RSA_size(rsa.get()) * 8;
  if (*key_size_bits != 1024 && *key_size_bits != 2048) {
    LOG(ERROR) << "Error parsing protection public key: Unsupported key size";
    return false;
  }
  if (!GetRsaModulus(*rsa, key_modulus)) {
    LOG(ERROR)
        << "Error parsing protection public key: Failed to extract key modulus";
    return false;
  }
  return true;
}

// Parses the public key that is protecting the sealed data into Trousers. The
// key size in bits via |key_size_bits|, and the RSA key public modulus is
// returned via |key_modulus|.
bool ParseAndLoadProtectionKey(TpmImpl* const tpm,
                               TSS_HCONTEXT tpm_context,
                               const Blob& public_key_spki_der,
                               int* key_size_bits,
                               TSS_HKEY* key_handle) {
  Blob key_modulus;
  if (!ParseProtectionKeySpki(public_key_spki_der, key_size_bits,
                              &key_modulus)) {
    LOG(ERROR) << "Failed to parse protection public key";
    return false;
  }
  UINT32 key_size_flag = 0;
  switch (*key_size_bits) {
    case 1024:
      key_size_flag = TSS_KEY_SIZE_1024;
      break;
    case 2048:
      key_size_flag = TSS_KEY_SIZE_2048;
      break;
    default:
      LOG(ERROR) << "Wrong size of protection public key";
      return false;
  }
  if (!tpm->CreateRsaPublicKeyObject(
          tpm_context, key_modulus,
          TSS_KEY_VOLATILE | TSS_KEY_TYPE_SIGNING | key_size_flag,
          TSS_SS_RSASSAPKCS1V15_SHA1, TSS_ES_NONE, key_handle)) {
    LOG(ERROR) << "Failed to load protection public key";
    return false;
  }
  return true;
}

// Loads the migration destination public key into Trousers. The loaded key
// handle is returned via |key_handle|.
bool LoadMigrationDestinationPublicKey(TpmImpl* const tpm,
                                       TSS_HCONTEXT tpm_context,
                                       const RSA& migration_destination_rsa,
                                       TSS_HKEY* key_handle) {
  Blob key_modulus;
  if (!GetRsaModulus(migration_destination_rsa, &key_modulus)) {
    LOG(ERROR) << "Error loading migration destination public key: Failed to "
                  "extract key modulus";
    return false;
  }
  if (!tpm->CreateRsaPublicKeyObject(tpm_context, key_modulus,
                                     TSS_KEY_VOLATILE | TSS_KEY_TYPE_STORAGE |
                                         kMigrationDestinationKeySizeFlag,
                                     TSS_SS_NONE, TSS_ES_RSAESOAEP_SHA1_MGF1,
                                     key_handle)) {
    LOG(ERROR) << "Error loading migration destination public key";
    return false;
  }
  return true;
}

// Obtains via the TPM_AuthorizeMigrationKey command the migration authorization
// blob for the given migration destination key. Returns the authorization blob
// via |migration_authorization_blob|.
bool ObtainMigrationAuthorization(TSS_HCONTEXT tpm_context,
                                  TSS_HTPM tpm_handle,
                                  TSS_HKEY migration_destination_key_handle,
                                  Blob* migration_authorization_blob) {
  uint32_t migration_authorization_blob_buf_size = 0;
  ScopedTssMemory migration_authorization_blob_buf(tpm_context);
  TSS_RESULT tss_result = Tspi_TPM_AuthorizeMigrationTicket(
      tpm_handle, migration_destination_key_handle,
      TSS_MS_RESTRICT_APPROVE_DOUBLE, &migration_authorization_blob_buf_size,
      migration_authorization_blob_buf.ptr());
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error obtaining the migration authorization: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  migration_authorization_blob->assign(
      migration_authorization_blob_buf.value(),
      migration_authorization_blob_buf.value() +
          migration_authorization_blob_buf_size);
  return true;
}

// Obtains via the TPM_CMK_CreateTicket command the CMK migration signature
// ticket for the signature of the challenge. Returns the ticket via
// |cmk_migration_signature_ticket|.
bool ObtainCmkMigrationSignatureTicket(
    TpmImpl* tpm,
    TSS_HCONTEXT tpm_context,
    TSS_HTPM tpm_handle,
    TSS_HKEY protection_key_handle,
    const Blob& migration_destination_key_pubkey,
    const Blob& cmk_pubkey,
    const Blob& protection_key_pubkey,
    const Blob& signed_challenge_value,
    Blob* cmk_migration_signature_ticket) {
  ScopedTssObject<TSS_HMIGDATA> migdata_handle(tpm_context);
  TSS_RESULT tss_result = Tspi_Context_CreateObject(
      tpm_context, TSS_OBJECT_TYPE_MIGDATA, 0, migdata_handle.ptr());
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error creating the CMK migration data object: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      migdata_handle, TSS_MIGATTRIB_MIGRATIONBLOB,
      TSS_MIGATTRIB_MIG_DESTINATION_PUBKEY_BLOB,
      migration_destination_key_pubkey.size(),
      const_cast<BYTE*>(migration_destination_key_pubkey.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the CMK migration destination public key: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(migdata_handle, TSS_MIGATTRIB_MIGRATIONBLOB,
                                  TSS_MIGATTRIB_MIG_SOURCE_PUBKEY_BLOB,
                                  cmk_pubkey.size(),
                                  const_cast<BYTE*>(cmk_pubkey.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the CMK migration source public key: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      migdata_handle, TSS_MIGATTRIB_MIGRATIONBLOB,
      TSS_MIGATTRIB_MIG_AUTHORITY_PUBKEY_BLOB, protection_key_pubkey.size(),
      const_cast<BYTE*>(protection_key_pubkey.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the CMK migration authority public key: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      migdata_handle, TSS_MIGATTRIB_TICKET_DATA, TSS_MIGATTRIB_TICKET_SIG_VALUE,
      signed_challenge_value.size(),
      const_cast<BYTE*>(signed_challenge_value.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the CMK migration signed challenge data: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_TPM_CMKCreateTicket(tpm_handle, protection_key_handle,
                                        migdata_handle);
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error obtaining the CMK migration signature ticket: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  SecureBlob local_cmk_migration_signature_ticket;
  if (tpm->GetDataAttribute(tpm_context, migdata_handle,
                             TSS_MIGATTRIB_TICKET_DATA,
                             TSS_MIGATTRIB_TICKET_SIG_TICKET,
                             &local_cmk_migration_signature_ticket)
      != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error reading the CMK migration signature ticket";
    return false;
  }
  // TODO(emaxx): Replace with a direct usage of Blob for the attribute read.
  cmk_migration_signature_ticket->assign(
      local_cmk_migration_signature_ticket.begin(),
      local_cmk_migration_signature_ticket.end());
  return true;
}

// Perform the migration of the CMK, passed in |srk_wrapped_cmk|, onto the key
// specified by |migration_destination_key_pubkey|, using the migration
// authorization from |migration_authorization_blob| and the CMK migration
// signature ticket from |cmk_migration_signature_ticket| for authorizing the
// migration. Returns the TPM_KEY12 blob of the migrated CMK via
// |migrated_cmk_key12_blob|, and the migration random XOR mask via
// |migration_random_blob| (see ExtractCmkPrivateKeyFromMigratedBlob() for the
// details).
bool MigrateCmk(TpmImpl* tpm,
                TSS_HCONTEXT tpm_context,
                TSS_HTPM tpm_handle,
                TSS_HKEY srk_handle,
                const Blob& srk_wrapped_cmk,
                const Blob& migration_destination_key_pubkey,
                const Blob& cmk_pubkey,
                const Blob& protection_key_pubkey,
                const Blob& migration_authorization_blob,
                const Blob& cmk_migration_signature_ticket,
                Blob* migrated_cmk_key12_blob,
                Blob* migration_random_blob) {
  // Load the wrapped CMK into Trousers.
  ScopedTssObject<TSS_HMIGDATA> wrapped_cmk_handle(tpm_context);
  TSS_RESULT tss_result = Tspi_Context_CreateObject(
      tpm_context, TSS_OBJECT_TYPE_RSAKEY, 0, wrapped_cmk_handle.ptr());
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error creating the wrapped certified migratable key object: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      wrapped_cmk_handle, TSS_TSPATTRIB_KEY_BLOB, TSS_TSPATTRIB_KEYBLOB_BLOB,
      srk_wrapped_cmk.size(), const_cast<BYTE*>(srk_wrapped_cmk.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the wrapped certified migratable key blob: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  // Prepare the parameters object for the migration command.
  ScopedTssObject<TSS_HMIGDATA> migdata_handle(tpm_context);
  tss_result = Tspi_Context_CreateObject(tpm_context, TSS_OBJECT_TYPE_MIGDATA,
                                         0, migdata_handle.ptr());
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error creating the CMK migration data object: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      migdata_handle, TSS_MIGATTRIB_MIGRATIONBLOB,
      TSS_MIGATTRIB_MIG_DESTINATION_PUBKEY_BLOB,
      migration_destination_key_pubkey.size(),
      const_cast<BYTE*>(migration_destination_key_pubkey.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the CMK migration destination public key: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(migdata_handle, TSS_MIGATTRIB_MIGRATIONBLOB,
                                  TSS_MIGATTRIB_MIG_SOURCE_PUBKEY_BLOB,
                                  cmk_pubkey.size(),
                                  const_cast<BYTE*>(cmk_pubkey.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the CMK migration source public key: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      migdata_handle, TSS_MIGATTRIB_MIGRATIONBLOB,
      TSS_MIGATTRIB_MIG_AUTHORITY_PUBKEY_BLOB, protection_key_pubkey.size(),
      const_cast<BYTE*>(protection_key_pubkey.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the CMK migration authority public key: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      migdata_handle, TSS_MIGATTRIB_MIGRATIONBLOB,
      TSS_MIGATTRIB_MIG_MSALIST_PUBKEY_BLOB, protection_key_pubkey.size(),
      const_cast<BYTE*>(protection_key_pubkey.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR)
        << "Error setting the CMK migration selection authority public key: "
        << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      migdata_handle, TSS_MIGATTRIB_MIGRATIONTICKET, 0,
      migration_authorization_blob.size(),
      const_cast<BYTE*>(migration_authorization_blob.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the CMK migration authorization: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      migdata_handle, TSS_MIGATTRIB_TICKET_DATA,
      TSS_MIGATTRIB_TICKET_SIG_TICKET, cmk_migration_signature_ticket.size(),
      const_cast<BYTE*>(cmk_migration_signature_ticket.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting the CMK migration signature ticket: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  // Perform the migration and extract the resulting data.
  UINT32 migration_random_buf_size = 0;
  ScopedTssMemory migration_random_buf(tpm_context);
  tss_result = Tspi_Key_CMKCreateBlob(
      wrapped_cmk_handle, srk_handle, migdata_handle,
      &migration_random_buf_size, migration_random_buf.ptr());
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error performing the certified migratable key migration: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  migration_random_blob->assign(
      migration_random_buf.value(),
      migration_random_buf.value() + migration_random_buf_size);
  SecureBlob local_migrated_cmk_key12_blob;
  if (tpm->GetDataAttribute(
          tpm_context, migdata_handle, TSS_MIGATTRIB_MIGRATIONBLOB,
          TSS_MIGATTRIB_MIG_XOR_BLOB, &local_migrated_cmk_key12_blob)
      != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to read the migrated key blob";
    return false;
  }
  // TODO(emaxx): Replace with a direct usage of Blob for the attribute read.
  migrated_cmk_key12_blob->assign(local_migrated_cmk_key12_blob.begin(),
                                  local_migrated_cmk_key12_blob.end());
  return true;
}

// Returns the digest of the blob of the TPM_MSA_COMPOSITE structure containing
// a sole reference to the specified key (whose TPM_PUBKEY blob is passed via
// |msa_pubkey_digest|).
Blob BuildMsaCompositeDigest(const Blob& msa_pubkey_digest) {
  // Build the structure.
  DCHECK_EQ(TPM_SHA1_160_HASH_LEN, msa_pubkey_digest.size());
  TPM_DIGEST digest;
  memcpy(digest.digest, msa_pubkey_digest.data(), msa_pubkey_digest.size());
  TPM_MSA_COMPOSITE msa_composite;
  msa_composite.MSAlist = 1;
  msa_composite.migAuthDigest = &digest;
  // Serialize the structure.
  UINT64 serializing_offset = 0;
  Trspi_LoadBlob_MSA_COMPOSITE(&serializing_offset, nullptr, &msa_composite);
  Blob msa_composite_blob(serializing_offset);
  serializing_offset = 0;
  Trspi_LoadBlob_MSA_COMPOSITE(&serializing_offset, msa_composite_blob.data(),
                               &msa_composite);
  return CryptoLib::Sha1(msa_composite_blob);
}

// Obtains via the TPM_CMK_ApproveMA command the migration authority approval
// ticket for the given TPM_MSA_COMPOSITE structure blob. Returns the ticket via
// |ma_approval_ticket|.
bool ObtainMaApprovalTicket(TpmImpl* const tpm,
                            TSS_HCONTEXT tpm_context,
                            TSS_HTPM tpm_handle,
                            const Blob& msa_composite_digest,
                            Blob* ma_approval_ticket) {
  ScopedTssObject<TSS_HMIGDATA> migdata_handle(tpm_context);
  TSS_RESULT tss_result = Tspi_Context_CreateObject(
      tpm_context, TSS_OBJECT_TYPE_MIGDATA, 0, migdata_handle.ptr());
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error creating migration data object: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_SetAttribData(
      migdata_handle, TSS_MIGATTRIB_AUTHORITY_DATA,
      TSS_MIGATTRIB_AUTHORITY_DIGEST, msa_composite_digest.size(),
      const_cast<BYTE*>(msa_composite_digest.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error setting migration selection authority: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  tss_result = Tspi_TPM_CMKApproveMA(tpm_handle, migdata_handle);
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error obtaining migration authority approval ticket: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  SecureBlob local_ma_approval_ticket;
  if (tpm->GetDataAttribute(
          tpm_context, migdata_handle, TSS_MIGATTRIB_AUTHORITY_DATA,
          TSS_MIGATTRIB_AUTHORITY_APPROVAL_HMAC, &local_ma_approval_ticket)
      != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Error reading migration authority approval ticket";
    return false;
  }
  // TODO(emaxx): Replace with a direct usage of Blob for the attribute read.
  ma_approval_ticket->assign(local_ma_approval_ticket.begin(),
                             local_ma_approval_ticket.end());
  return true;
}

// Parses the TPM_KEY12 blob and returns its "encData" field blob.
bool ParseEncDataFromKey12Blob(const Blob& key12_blob, Blob* enc_data) {
  using ScopedByteArray = std::unique_ptr<BYTE, base::FreeDeleter>;
  ScopedKey12 key12;
  UINT64 key12_parsing_offset = 0;
  TSS_RESULT tss_result = Trspi_UnloadBlob_KEY12(
      &key12_parsing_offset, const_cast<BYTE*>(key12_blob.data()), key12.ptr());
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Failed to parse the migrated key TPM_KEY12 blob: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  if (key12_parsing_offset != key12_blob.size()) {
    LOG(ERROR) << "Failed to parse the migrated key TPM_KEY12 blob due to size "
                  "mismatch";
    return false;
  }
  enc_data->assign(key12->encData, key12->encData + key12->encSize);
  return true;
}

// Applies to the given blob the element-to-element bitwise XOR against the
// other blob.
void XorBytes(uint8_t* inplace_target_begin,
              const uint8_t* other_begin,
              size_t size) {
  for (size_t index = 0; index < size; ++index)
    inplace_target_begin[index] ^= other_begin[index];
}

// Obtains the value from its MGF1-masked representation in |masked_value|. The
// input value for the MGF1 mask is passed via |mgf_input_value|. Returns the
// result via |value|; its length, on success, is guaranteed to be the same as
// the |masked_value|'s one.
bool UnmaskWithMgf1(const SecureBlob& masked_value,
                    const SecureBlob& mgf_input_value,
                    SecureBlob* value) {
  if (masked_value.empty()) {
    LOG(ERROR) << "Bad MGF1-masked value";
    return false;
  }
  if (mgf_input_value.empty()) {
    LOG(ERROR) << "Bad MGF1 input value";
    return false;
  }
  SecureBlob mask(masked_value.size());
  TSS_RESULT tss_result = Trspi_MGF1(TSS_HASH_SHA1, mgf_input_value.size(),
                                     const_cast<BYTE*>(mgf_input_value.data()),
                                     mask.size(), mask.data());
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Failed to generate the MGF1 mask: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  *value = masked_value;
  XorBytes(value->data(), mask.data(), value->size());
  return true;
}

// Performs the RSA OAEP MGF1 decoding of the encoded blob |encoded_blob| using
// the OAEP label parameter equal to |oaep_label|. The size |message_length|
// specifies the expected size of the returned message.
// Returns the decoded message via |message| and the OAEP seed via |seed|.
// Note that this custom implementation is used instead of the one from OpenSSL,
// because we need to get the seed back and OpenSSL doesn't return it.
bool DecodeOaepMgf1Encoding(const Blob& encoded_blob,
                            size_t message_length,
                            const Blob& oaep_label,
                            SecureBlob* seed,
                            SecureBlob* message) {
  // The comments in this function below refer to the notation that corresponds
  // to the "RSAES-OAEP Encryption Scheme" Algorithm specification and
  // supporting documentation (2000), the "EME-OAEP-Decode" section.
  // The correspondence between the function parameters and the terms in the
  // specification is:
  // * |encoded_blob| - "EM";
  // * |message_length| - "mLen";
  // * |oaep_label| - "P";
  // * |seed| - "seed";
  // * |message| - "M".
  // Note that as the MGF1 mask is used which is based on SHA-1, the "hLen" term
  // corresponds to |SHA_DIGEST_LENGTH|.
  const size_t blob_size = encoded_blob.size();
  // Step #1 is omitted as not applicable to our implementation - the length of
  // |oaep_label| can't realistically reach the size constraint of SHA-1.
  // Step #2. The total length of the encoded message is formed by the length of
  // "seed" (which is equal to "hLen"), the length of "pHash" (which is also
  // equal to "hLen"), the length of the original message, and the length of the
  // "01" octet (which is 1 byte).
  const size_t minimum_blob_size = 2 * SHA_DIGEST_LENGTH + 1 + message_length;
  if (blob_size < minimum_blob_size) {
    LOG(ERROR) << "Failed to parse the blob: the size is too small";
    return false;
  }
  // Step #3. Split "EM" into "maskedSeed" and "maskedDB".
  const SecureBlob masked_seed(encoded_blob.begin(),
                               encoded_blob.begin() + SHA_DIGEST_LENGTH);
  const SecureBlob masked_padded_message(
      encoded_blob.begin() + SHA_DIGEST_LENGTH, encoded_blob.end());
  // Steps ##4-5. Unmask "maskedSeed" to obtain "seed".
  if (!UnmaskWithMgf1(masked_seed, masked_padded_message, seed)) {
    LOG(ERROR) << "Failed to unmask the seed";
    return false;
  }
  // Steps ##6-7. Unmask "maskedDB" into "DB".
  SecureBlob padded_message;
  if (!UnmaskWithMgf1(masked_padded_message, *seed, &padded_message)) {
    LOG(ERROR) << "Failed to unmask the message";
    return false;
  }
  // Steps ##8-10. Extract "M" from "DB", extract "pHash" from "DB" and check it
  // against "P", and verify the zeros/ones padding that covers the rest.
  const Blob obtained_label_digest(padded_message.begin(),
                                   padded_message.begin() + SHA_DIGEST_LENGTH);
  const Blob obtained_zeroes_ones_padding(
      padded_message.begin() + SHA_DIGEST_LENGTH,
      padded_message.end() - message_length);
  message->assign(padded_message.end() - message_length, padded_message.end());
  DCHECK_EQ(padded_message.size(), obtained_label_digest.size() +
                                       obtained_zeroes_ones_padding.size() +
                                       message->size());
  if (obtained_label_digest != CryptoLib::Sha1(oaep_label)) {
    LOG(ERROR) << "Incorrect OAEP label";
    return false;
  }
  const Blob expected_zeroes_ones_padding =
      CombineBlobs({Blob(obtained_zeroes_ones_padding.size() - 1), Blob(1, 1)});
  if (obtained_zeroes_ones_padding != expected_zeroes_ones_padding) {
    LOG(ERROR) << "Incorrect zeroes block in OAEP padding";
    return false;
  }
  return true;
}

// Parses an unsigned four-byte integer from the given position in the blob in
// the TPM endianness.
uint32_t DecodeTpmUint32(const uint8_t* begin) {
  UINT64 parsing_offset = 0;
  uint32_t result = 0;
  Trspi_UnloadBlob_UINT32(&parsing_offset, &result, const_cast<BYTE*>(begin));
  DCHECK_EQ(4, parsing_offset);
  return result;
}

// Parses the RSA secret prime from the TPM_MIGRATE_ASYMKEY blob and the seed
// blob.
bool ParseRsaSecretPrimeFromTpmMigrateAsymkeyBlob(
    const SecureBlob& tpm_migrate_asymkey_blob,
    const SecureBlob& tpm_migrate_asymkey_oaep_seed_blob,
    SecureBlob* secret_prime_blob) {
  DCHECK_EQ(SHA_DIGEST_LENGTH, tpm_migrate_asymkey_oaep_seed_blob.size());
  // The binary layout, as specified in TPM 1.2 Part 3 Section 11.9
  // ("TPM_CMK_CreateBlob"), is:
  // * |tpm_migrate_asymkey_oaep_seed_blob| (called "K1" in the specification):
  //   is of |SHA_DIGEST_LENGTH| bytes length, and is structured as following:
  //   * the first 4 bytes contain a four-byte integer - the size of the private
  //     key in bytes (obtained from TPM_STORE_PRIVKEY.keyLength);
  //   * the rest are the first |kMigratedCmkPrivateKeySeedPartSizeBytes| bytes
  //     of the private key;
  // * |tpm_migrate_asymkey_blob| (called "M1" in the specification): the binary
  //   dump of the TPM_MIGRATE_ASYMKEY structure, of which we are looking only
  //   at:
  //   * the first field |payload| of length 1 byte, which has to be equal to
  //     |TPM_PT_CMK_MIGRATE|;
  //   * the last field |partPrivKey|, which contains the last
  //     |kMigratedCmkPrivateKeyRestPartSizeBytes| bytes of the private key;
  //   * the last but one field |partPrivKeyLen| of length 4 bytes, which is a
  //     four-byte integer that has to be equal to
  //     |kMigratedCmkPrivateKeySeedPartSizeBytes|.
  // We parse and validate this data below:
  // Parse and validate the keyLength field of the TPM_STORE_PRIVKEY structure.
  DCHECK_GE(tpm_migrate_asymkey_oaep_seed_blob.size(), 4);
  const uint32_t tpm_store_privkey_key_length =
      DecodeTpmUint32(tpm_migrate_asymkey_oaep_seed_blob.data());
  if (tpm_store_privkey_key_length != kCmkPrivateKeySizeBytes) {
    LOG(ERROR) << "Wrong migrated private key size";
    return false;
  }
  // Extract the part of the private key from the OAEP seed.
  const SecureBlob tpm_store_privkey_key_seed_part_blob(
      tpm_migrate_asymkey_oaep_seed_blob.begin() + 4,
      tpm_migrate_asymkey_oaep_seed_blob.end());
  DCHECK_EQ(kMigratedCmkPrivateKeySeedPartSizeBytes,
            tpm_store_privkey_key_seed_part_blob.size());
  // Validate the TPM_MIGRATE_ASYMKEY blob size.
  if (tpm_migrate_asymkey_blob.size() <
      kMigratedCmkPrivateKeyRestPartSizeBytes + 4) {
    LOG(ERROR) << "Wrong length of TPM_MIGRATE_ASYMKEY blob";
    return false;
  }
  // Parse and validate the payload field of the TPM_MIGRATE_ASYMKEY structure.
  const int tpm_migrate_asymkey_payload = tpm_migrate_asymkey_blob[0];
  if (tpm_migrate_asymkey_payload != TPM_PT_CMK_MIGRATE) {
    LOG(ERROR) << "Wrong migration payload type";
    return false;
  }
  // Extract the part of the private key from the TPM_MIGRATE_ASYMKEY blob.
  const SecureBlob tpm_store_privkey_key_rest_part_blob(
      tpm_migrate_asymkey_blob.end() - kMigratedCmkPrivateKeyRestPartSizeBytes,
      tpm_migrate_asymkey_blob.end());
  // Parse and validate the partPrivKeyLen field of the TPM_MIGRATE_ASYMKEY
  // structure.
  const uint32_t tpm_migrate_asymkey_part_priv_key_length = DecodeTpmUint32(
      &tpm_migrate_asymkey_blob[tpm_migrate_asymkey_blob.size() -
                                kMigratedCmkPrivateKeyRestPartSizeBytes - 4]);
  if (tpm_migrate_asymkey_part_priv_key_length !=
      kMigratedCmkPrivateKeyRestPartSizeBytes) {
    LOG(ERROR) << "Wrong size of the private key part in TPM_MIGRATE_ASYMKEY";
    return false;
  }
  // Assemble the resulting secret prime blob.
  *secret_prime_blob =
      SecureBlob::Combine(tpm_store_privkey_key_seed_part_blob,
                          tpm_store_privkey_key_rest_part_blob);
  DCHECK_EQ(kCmkPrivateKeySizeBytes, secret_prime_blob->size());
  return true;
}

// Extract the CMK's private key from the output of the migration procedure: the
// TPM_KEY12 blob of the migrated CMK in |migrated_cmk_key12_blob|, and the
// migration random XOR-mask in |migration_random_blob|. Returns the CMK's
// secret RSA prime blob via |private_key_blob|.
bool ExtractCmkPrivateKeyFromMigratedBlob(const Blob& migrated_cmk_key12_blob,
                                          const Blob& migration_random_blob,
                                          const Blob& cmk_pubkey_digest,
                                          const Blob& msa_composite_digest,
                                          RSA* migration_destination_rsa,
                                          SecureBlob* private_key_blob) {
  // Load the encrypted TPM_MIGRATE_ASYMKEY blob from the TPM_KEY12 blob.
  // Note that this encrypted TPM_MIGRATE_ASYMKEY blob was generated by taking
  // the TPM_MIGRATE_ASYMKEY blob, applying the RSA OAEP *encoding* (not
  // encryption), XOR'ing it with the migration random XOR-mask, applying the
  // RSA OAEP *encryption* (not encoding). We'll unwind this to obtain the
  // original TPM_MIGRATE_ASYMKEY blob below.
  Blob encrypted_tpm_migrate_asymkey_blob;
  if (!ParseEncDataFromKey12Blob(migrated_cmk_key12_blob,
                                 &encrypted_tpm_migrate_asymkey_blob)) {
    LOG(ERROR) << "Failed to parse the encrypted TPM_MIGRATE_ASYMKEY blob from "
                  "the TPM_KEY12 blob";
    return false;
  }
  if (encrypted_tpm_migrate_asymkey_blob.size() !=
      kMigrationDestinationKeySizeBytes) {
    LOG(ERROR) << "Failed to parse the encrypted TPM_MIGRATE_ASYMKEY blob due "
                  "to size mismatch";
    return false;
  }
  // Perform the RSA OAEP decryption of the encrypted TPM_MIGRATE_ASYMKEY blob,
  // using the custom OAEP label parameter as prescribed by the TPM 1.2 specs.
  SecureBlob decrypted_tpm_migrate_asymkey_blob;
  if (!CryptoLib::RsaOaepDecrypt(SecureBlob(encrypted_tpm_migrate_asymkey_blob),
                                 SecureBlob(kTpmRsaOaepLabel),
                                 migration_destination_rsa,
                                 &decrypted_tpm_migrate_asymkey_blob)) {
    LOG(ERROR)
        << "Failed to RSA-decrypt the encrypted TPM_MIGRATE_ASYMKEY blob";
    return false;
  }
  if (decrypted_tpm_migrate_asymkey_blob.size() !=
      migration_random_blob.size()) {
    LOG(ERROR)
        << "Failed to decrypt TPM_MIGRATE_ASYMKEY blob due to size mismatch";
    return false;
  }
  // XOR the decrypted TPM_MIGRATE_ASYMKEY blob with the migration random
  // XOR-mask.
  DCHECK_EQ(decrypted_tpm_migrate_asymkey_blob.size(),
            migration_random_blob.size());
  SecureBlob xored_decrypted_tpm_migrate_asymkey_blob =
      decrypted_tpm_migrate_asymkey_blob;
  XorBytes(xored_decrypted_tpm_migrate_asymkey_blob.data(),
           migration_random_blob.data(),
           xored_decrypted_tpm_migrate_asymkey_blob.size());
  // Perform the RSA OAEP decoding (not decryption) of the XOR'ed decrypted
  // TPM_MIGRATE_ASYMKEY blob.
  // The OAEP label parameter is equal to concatenation of
  // |msa_composite_digest| and |cmk_pubkey_digest|.
  // The OAEP seed parameter is extracted as well, because it contains a part of
  // the private key data.
  // Note that our own implementation of OAEP decoding is used instead of the
  // OpenSSL's one, as the latter doesn't return the decoded seed.
  const Blob tpm_migrate_asymkey_oaep_label_blob =
      CombineBlobs({msa_composite_digest, cmk_pubkey_digest});
  SecureBlob tpm_migrate_asymkey_oaep_seed_blob;
  SecureBlob tpm_migrate_asymkey_blob;
  if (!DecodeOaepMgf1Encoding(
          xored_decrypted_tpm_migrate_asymkey_blob, kTpmMigrateAsymkeyBlobSize,
          tpm_migrate_asymkey_oaep_label_blob,
          &tpm_migrate_asymkey_oaep_seed_blob, &tpm_migrate_asymkey_blob)) {
    LOG(ERROR) << "Failed to perform RSA OAEP decoding of the XOR'ed decrypted "
                  "TPM_MIGRATE_ASYMKEY blob";
    return false;
  }
  // Parse the resulting CMK's secret prime from the TPM_MIGRATE_ASYMKEY blob
  // and the seed blob.
  if (!ParseRsaSecretPrimeFromTpmMigrateAsymkeyBlob(
          tpm_migrate_asymkey_blob, tpm_migrate_asymkey_oaep_seed_blob,
          private_key_blob)) {
    LOG(ERROR)
        << "Failed to parse the private key from the TPM_MIGRATE_ASYMKEY blob";
    return false;
  }
  DCHECK_EQ(kCmkPrivateKeySizeBytes, private_key_blob->size());
  return true;
}

// Generate the Certified Migratable Key, associated with the protection public
// key (via the TPM_MSA_COMPOSITE digest passed by |msa_composite_digest|). The
// |ma_approval_ticket| should contain ticket obtained from the
// TPM_CMK_ApproveMA command. Returns the CMK TPM_PUBKEY blob via |cmk_pubkey|
// and the wrapped CMK blob via |srk_wrapped_cmk|.
bool GenerateCmk(TpmImpl* const tpm,
                 TSS_HCONTEXT tpm_context,
                 TSS_HTPM tpm_handle,
                 TSS_HKEY srk_handle,
                 const Blob& msa_composite_digest,
                 const Blob& ma_approval_ticket,
                 Blob* cmk_pubkey,
                 Blob* srk_wrapped_cmk) {
  // Create the Certified Migratable Key object. Note that the actual key
  // generation isn't happening at this point yet.
  ScopedTssKey cmk_handle(tpm_context);
  TSS_RESULT tss_result = Tspi_Context_CreateObject(
      tpm_context, TSS_OBJECT_TYPE_RSAKEY,
      TSS_KEY_STRUCT_KEY12 | TSS_KEY_VOLATILE | TSS_KEY_TYPE_STORAGE |
          TSS_KEY_AUTHORIZATION | TSS_KEY_MIGRATABLE |
          TSS_KEY_CERTIFIED_MIGRATABLE | kCmkKeySizeFlag,
      cmk_handle.ptr());
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Failed to create certified migratable key object: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  // Set the parameter to make the created CMK associated with the protection
  // public key (via the TPM_MSA_COMPOSITE digest).
  tss_result = Tspi_SetAttribData(
      cmk_handle, TSS_TSPATTRIB_KEY_CMKINFO,
      TSS_TSPATTRIB_KEYINFO_CMK_MA_DIGEST, msa_composite_digest.size(),
      const_cast<BYTE*>(msa_composite_digest.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Failed to set migration authority digest: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  // Set the parameter to pass the migration authority approval ticket to the
  // CMK creation procedure.
  tss_result = Tspi_SetAttribData(cmk_handle, TSS_TSPATTRIB_KEY_CMKINFO,
                                  TSS_TSPATTRIB_KEYINFO_CMK_MA_APPROVAL,
                                  ma_approval_ticket.size(),
                                  const_cast<BYTE*>(ma_approval_ticket.data()));
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Failed to set migration authority approval ticket: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  // Add the usage policy to the CMK. The policy will effectively disallow the
  // usage of the CMK for signing/decryption, as the policy's password is
  // discarded.
  ScopedTssPolicy usage_policy_handle(tpm_context);
  if (!tpm->CreatePolicyWithRandomPassword(tpm_context, TSS_POLICY_USAGE,
                                           usage_policy_handle.ptr())) {
    LOG(ERROR) << "Failed to create the usage policy";
    return false;
  }
  tss_result = Tspi_Policy_AssignToObject(usage_policy_handle, cmk_handle);
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Error assigning the usage policy to the key: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  // Add the migration policy to the CMK. The policy will effectively disallow
  // the usage of the CMK for non-certified migration, as the policy's password
  // is discarded.
  ScopedTssPolicy migration_policy_handle(tpm_context);
  if (!tpm->CreatePolicyWithRandomPassword(tpm_context, TSS_POLICY_MIGRATION,
                                           migration_policy_handle.ptr())) {
    LOG(ERROR) << "Failed to create the usage policy";
    return false;
  }
  tss_result = Tspi_Policy_AssignToObject(migration_policy_handle, cmk_handle);
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Failed to set the migration policy to the key: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  // Trigger the CMK generation and extract the resulting blobs.
  tss_result =
      Tspi_Key_CreateKey(cmk_handle, srk_handle, 0 /* hPcrComposite */);
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Failed to create the certified migratable key: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  SecureBlob local_cmk_pubkey;
  if (tpm->GetDataAttribute(tpm_handle, cmk_handle, TSS_TSPATTRIB_KEY_BLOB,
                             TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY,
                             &local_cmk_pubkey) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to read the certified migratable public key";
    return false;
  }
  // TODO(emaxx): Replace with a direct usage of Blob for the attribute read.
  cmk_pubkey->assign(local_cmk_pubkey.begin(), local_cmk_pubkey.end());
  SecureBlob local_srk_wrapped_cmk;
  if (tpm->GetDataAttribute(tpm_handle, cmk_handle, TSS_TSPATTRIB_KEY_BLOB,
                             TSS_TSPATTRIB_KEYBLOB_BLOB,
                             &local_srk_wrapped_cmk) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to read the certified migratable key";
    return false;
  }
  // TODO(emaxx): Replace with a direct usage of Blob for the attribute read.
  srk_wrapped_cmk->assign(local_srk_wrapped_cmk.begin(),
                          local_srk_wrapped_cmk.end());
  return true;
}

UnsealingSessionTpm1Impl::UnsealingSessionTpm1Impl(
    TpmImpl* tpm,
    const Blob& srk_wrapped_cmk,
    const Blob& public_key_spki_der,
    const Blob& delegate_blob,
    const Blob& delegate_secret,
    const Blob& cmk_pubkey,
    const Blob& protection_key_pubkey,
    crypto::ScopedRSA migration_destination_rsa,
    const Blob& migration_destination_key_pubkey)
    : tpm_(tpm),
      srk_wrapped_cmk_(srk_wrapped_cmk),
      public_key_spki_der_(public_key_spki_der),
      delegate_blob_(delegate_blob),
      delegate_secret_(delegate_secret),
      cmk_pubkey_(cmk_pubkey),
      cmk_pubkey_digest_(CryptoLib::Sha1(cmk_pubkey_)),
      protection_key_pubkey_(protection_key_pubkey),
      protection_key_pubkey_digest_(CryptoLib::Sha1(protection_key_pubkey_)),
      migration_destination_rsa_(std::move(migration_destination_rsa)),
      migration_destination_key_pubkey_(migration_destination_key_pubkey),
      migration_destination_key_pubkey_digest_(
          CryptoLib::Sha1(migration_destination_key_pubkey_)),
      msa_composite_digest_(
          BuildMsaCompositeDigest(protection_key_pubkey_digest_)) {}

UnsealingSessionTpm1Impl::~UnsealingSessionTpm1Impl() = default;

SignatureSealingBackend::Algorithm
UnsealingSessionTpm1Impl::GetChallengeAlgorithm() {
  return Algorithm::kRsassaPkcs1V15Sha1;
}

Blob UnsealingSessionTpm1Impl::GetChallengeValue() {
  return CombineBlobs({protection_key_pubkey_digest_,
                       migration_destination_key_pubkey_digest_,
                       cmk_pubkey_digest_});
}

bool UnsealingSessionTpm1Impl::Unseal(const Blob& signed_challenge_value,
                                      SecureBlob* unsealed_value) {
  // Obtain the TPM context and handle with the required authorization.
  ScopedTssContext tpm_context;
  TSS_HTPM tpm_handle = 0;
  if (!tpm_->ConnectContextAsDelegate(SecureBlob(delegate_blob_),
                                      SecureBlob(delegate_secret_),
                                      tpm_context.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Failed to connect to the TPM";
    return false;
  }
  // Load the required keys into Trousers.
  ScopedTssKey srk_handle(tpm_context);
  TSS_RESULT tss_result = TSS_SUCCESS;
  if (!tpm_->LoadSrk(tpm_context, srk_handle.ptr(), &tss_result)) {
    LOG(ERROR) << "Failed to load the SRK: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  int protection_key_size_bits = 0;
  ScopedTssKey protection_key_handle(tpm_context);
  if (!ParseAndLoadProtectionKey(tpm_, tpm_context, public_key_spki_der_,
                                 &protection_key_size_bits,
                                 protection_key_handle.ptr())) {
    LOG(ERROR) << "Failed to load the protection public key";
    return false;
  }
  ScopedTssKey migration_destination_key_handle(tpm_context);
  if (!LoadMigrationDestinationPublicKey(
          tpm_, tpm_context, *migration_destination_rsa_,
          migration_destination_key_handle.ptr())) {
    LOG(ERROR) << "Failed to load the migration destination key";
    return false;
  }
  // Sanity check the received signature blob.
  if (signed_challenge_value.size() != protection_key_size_bits / 8) {
    LOG(ERROR) << "Wrong size of challenge signature blob";
    return false;
  }
  // Obtain the migration authorization blob for the migration destination key.
  Blob migration_authorization_blob;
  if (!ObtainMigrationAuthorization(tpm_context, tpm_handle,
                                    migration_destination_key_handle,
                                    &migration_authorization_blob)) {
    LOG(ERROR) << "Failed to obtain the migration authorization";
    return false;
  }
  // Obtain the CMK migration signature ticket for the signed challenge blob.
  Blob cmk_migration_signature_ticket;
  if (!ObtainCmkMigrationSignatureTicket(
          tpm_, tpm_context, tpm_handle, protection_key_handle,
          migration_destination_key_pubkey_, cmk_pubkey_,
          protection_key_pubkey_, signed_challenge_value,
          &cmk_migration_signature_ticket)) {
    LOG(ERROR) << "Failed to obtain the CMK migration signature ticket";
    return false;
  }
  // Perform the migration of the CMK onto the migration destination key.
  Blob migrated_cmk_key12_blob;
  Blob migration_random_blob;
  if (!MigrateCmk(tpm_, tpm_context, tpm_handle, srk_handle, srk_wrapped_cmk_,
                  migration_destination_key_pubkey_, cmk_pubkey_,
                  protection_key_pubkey_, migration_authorization_blob,
                  cmk_migration_signature_ticket, &migrated_cmk_key12_blob,
                  &migration_random_blob)) {
    LOG(ERROR) << "Failed to migrate the certified migratable key";
    return false;
  }
  // Decrypt and decode the CMK private key. Return the digest of the raw RSA
  // prime, to avoid any potential bias.
  SecureBlob cmk_private_key;
  if (!ExtractCmkPrivateKeyFromMigratedBlob(
          migrated_cmk_key12_blob, migration_random_blob, cmk_pubkey_digest_,
          msa_composite_digest_, migration_destination_rsa_.get(),
          &cmk_private_key)) {
    LOG(ERROR) << "Failed to extract the certified migratable private key";
    return false;
  }
  *unsealed_value = CryptoLib::Sha256(cmk_private_key);
  return true;
}

}  // namespace

SignatureSealingBackendTpm1Impl::SignatureSealingBackendTpm1Impl(TpmImpl* tpm)
    : tpm_(tpm) {}

SignatureSealingBackendTpm1Impl::~SignatureSealingBackendTpm1Impl() = default;

bool SignatureSealingBackendTpm1Impl::CreateSealedSecret(
    const Blob& public_key_spki_der,
    const std::vector<Algorithm>& key_algorithms,
    const std::map<uint32_t, Blob>& /* pcr_values */,
    const Blob& delegate_blob,
    const Blob& delegate_secret,
    SignatureSealedData* sealed_secret_data) {
  // Only the |kRsassaPkcs1V15Sha1| algorithm is supported.
  if (std::find(key_algorithms.begin(), key_algorithms.end(),
                Algorithm::kRsassaPkcs1V15Sha1) == key_algorithms.end()) {
    LOG(ERROR) << "The key doesn't support RSASSA-PKCS1-v1_5 with SHA-1";
    return false;
  }
  // Obtain the TPM context and handle with the required authorization.
  ScopedTssContext tpm_context;
  TSS_HTPM tpm_handle = 0;
  if (!tpm_->ConnectContextAsDelegate(SecureBlob(delegate_blob),
                                      SecureBlob(delegate_secret),
                                      tpm_context.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Failed to connect to the TPM";
    return false;
  }
  // Load the protection public key into Trousers. Obtain its TPM_PUBKEY blob
  // and build the blob of the TPM_MSA_COMPOSITE structure containing a sole
  // reference to this key.
  int protection_key_size_bits = 0;
  ScopedTssKey protection_key_handle(tpm_context);
  if (!ParseAndLoadProtectionKey(tpm_, tpm_context, public_key_spki_der,
                                 &protection_key_size_bits,
                                 protection_key_handle.ptr())) {
    LOG(ERROR) << "Failed to load the protection public key";
    return false;
  }
  SecureBlob protection_key_pubkey;
  if (tpm_->GetDataAttribute(
          tpm_context, protection_key_handle, TSS_TSPATTRIB_KEY_BLOB,
          TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY, &protection_key_pubkey)
      != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to read the protection public key";
    return false;
  }
  const Blob protection_key_pubkey_digest =
      CryptoLib::Sha1(protection_key_pubkey);
  const Blob msa_composite_digest =
      BuildMsaCompositeDigest(protection_key_pubkey_digest);
  // Obtain the migration authority approval ticket for the TPM_MSA_COMPOSITE
  // structure.
  Blob ma_approval_ticket;
  if (!ObtainMaApprovalTicket(tpm_, tpm_context, tpm_handle,
                              msa_composite_digest, &ma_approval_ticket)) {
    LOG(ERROR) << "Failed to obtain the migration authority approval ticket";
    return false;
  }
  // Load the SRK.
  ScopedTssKey srk_handle(tpm_context);
  TSS_RESULT tss_result = TSS_SUCCESS;
  if (!tpm_->LoadSrk(tpm_context, srk_handle.ptr(), &tss_result)) {
    LOG(ERROR) << "Failed to load the SRK: "
               << FormatTrousersErrorCode(tss_result);
    return false;
  }
  // Generate the Certified Migratable Key, associated with the protection
  // public key (via the TPM_MSA_COMPOSITE digest). Obtain the resulting wrapped
  // CMK blob and the TPM_PUBKEY blob.
  Blob cmk_pubkey;
  Blob srk_wrapped_cmk;
  if (!GenerateCmk(tpm_, tpm_context, tpm_handle, srk_handle,
                   msa_composite_digest, ma_approval_ticket, &cmk_pubkey,
                   &srk_wrapped_cmk)) {
    LOG(ERROR) << "Failed to generate the certified migratable key";
    return false;
  }
  // Fill the resulting proto with data required for unsealing.
  sealed_secret_data->Clear();
  SignatureSealedData_Tpm12CertifiedMigratableKeyData* const
      sealed_data_contents =
          sealed_secret_data->mutable_tpm12_certified_migratable_key_data();
  sealed_data_contents->set_public_key_spki_der(
      BlobToString(public_key_spki_der));
  sealed_data_contents->set_srk_wrapped_cmk(BlobToString(srk_wrapped_cmk));
  sealed_data_contents->set_cmk_pubkey(BlobToString(cmk_pubkey));
  return true;
}

std::unique_ptr<SignatureSealingBackend::UnsealingSession>
SignatureSealingBackendTpm1Impl::CreateUnsealingSession(
    const SignatureSealedData& sealed_secret_data,
    const Blob& public_key_spki_der,
    const std::vector<Algorithm>& key_algorithms,
    const Blob& delegate_blob,
    const Blob& delegate_secret) {
  // Validate the parameters.
  if (!sealed_secret_data.has_tpm12_certified_migratable_key_data()) {
    LOG(ERROR) << "Sealed data is empty or uses unexpected method";
    return nullptr;
  }
  const SignatureSealedData_Tpm12CertifiedMigratableKeyData&
      sealed_data_contents =
          sealed_secret_data.tpm12_certified_migratable_key_data();
  if (sealed_data_contents.public_key_spki_der() !=
      BlobToString(public_key_spki_der)) {
    LOG(ERROR) << "Wrong subject public key info";
    return nullptr;
  }
  if (std::find(key_algorithms.begin(), key_algorithms.end(),
                Algorithm::kRsassaPkcs1V15Sha1) == key_algorithms.end()) {
    LOG(ERROR) << "Failed to choose the algorithm: the key doesn't support "
                  "RSASSA-PKCS1-v1_5 with SHA-1";
    return nullptr;
  }
  // Obtain the TPM context and handle with the required authorization.
  ScopedTssContext tpm_context;
  TSS_HTPM tpm_handle = 0;
  if (!tpm_->ConnectContextAsDelegate(SecureBlob(delegate_blob),
                                      SecureBlob(delegate_secret),
                                      tpm_context.ptr(), &tpm_handle)) {
    LOG(ERROR) << "Failed to connect to the TPM";
    return nullptr;
  }
  // Obtain the TPM_PUBKEY blob for the protection key.
  int protection_key_size_bits = 0;
  ScopedTssKey protection_key_handle(tpm_context);
  if (!ParseAndLoadProtectionKey(tpm_, tpm_context, public_key_spki_der,
                                 &protection_key_size_bits,
                                 protection_key_handle.ptr())) {
    LOG(ERROR) << "Failed to load the protection public key";
    return nullptr;
  }
  SecureBlob protection_key_pubkey;
  if (tpm_->GetDataAttribute(
          tpm_context, protection_key_handle, TSS_TSPATTRIB_KEY_BLOB,
          TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY, &protection_key_pubkey)
      != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to read the protection public key";
    return nullptr;
  }
  // Generate the migration destination RSA key. Onto this key the CMK private
  // key will be migrated; to complete the unsealing, the decryption operation
  // using the migration destination key will be performed. The security
  // properties of the migration destination key aren't crucial, besides the
  // reasonable amount of entropy, therefore generating using OpenSSL is fine.
  // TODO(emaxx): Do the RSA key generation in background in advance.
  crypto::ScopedRSA migration_destination_rsa(RSA_generate_key(
      kMigrationDestinationKeySizeBits, kWellKnownExponent, nullptr, nullptr));
  if (!migration_destination_rsa) {
    LOG(ERROR) << "Failed to generate the migration destination key";
    return nullptr;
  }
  // Obtain the TPM_PUBKEY blob for the migration destination key.
  ScopedTssKey migration_destination_key_handle(tpm_context);
  if (!LoadMigrationDestinationPublicKey(
          tpm_, tpm_context, *migration_destination_rsa,
          migration_destination_key_handle.ptr())) {
    LOG(ERROR) << "Failed to load the migration destination key";
    return nullptr;
  }
  SecureBlob migration_destination_key_pubkey;
  if (tpm_->GetDataAttribute(tpm_context, migration_destination_key_handle,
                              TSS_TSPATTRIB_KEY_BLOB,
                              TSS_TSPATTRIB_KEYBLOB_PUBLIC_KEY,
                              &migration_destination_key_pubkey)
      != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to read the migration destination public key";
    return nullptr;
  }
  return std::make_unique<UnsealingSessionTpm1Impl>(
      tpm_, BlobFromString(sealed_data_contents.srk_wrapped_cmk()),
      public_key_spki_der, delegate_blob, delegate_secret,
      BlobFromString(sealed_data_contents.cmk_pubkey()), protection_key_pubkey,
      std::move(migration_destination_rsa), migration_destination_key_pubkey);
}

}  // namespace cryptohome
