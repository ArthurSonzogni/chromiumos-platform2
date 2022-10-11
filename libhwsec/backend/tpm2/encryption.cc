// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/encryption.h"

#include <memory>
#include <string>

#include <base/callback_helpers.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trunks/openssl_utility.h>
#include <trunks/tpm_utility.h>

#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/no_default_init.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

struct SchemaDetail {
  NoDefault<trunks::TPM_ALG_ID> schema;
  NoDefault<trunks::TPM_ALG_ID> hash_alg;
};

StatusOr<SchemaDetail> GetSchemaDetail(
    const EncryptionTpm2::EncryptionOptions& options) {
  switch (options.schema) {
    case EncryptionTpm2::EncryptionOptions::Schema::kDefault:
      return SchemaDetail{
          .schema = trunks::TPM_ALG_OAEP,
          .hash_alg = trunks::TPM_ALG_SHA256,
      };
    case EncryptionTpm2::EncryptionOptions::Schema::kNull:
      return SchemaDetail{
          .schema = trunks::TPM_ALG_NULL,
          .hash_alg = trunks::TPM_ALG_NULL,
      };
    case EncryptionTpm2::EncryptionOptions::Schema::kRsaesSha1:
      return SchemaDetail{
          .schema = trunks::TPM_ALG_RSAES,
          .hash_alg = trunks::TPM_ALG_SHA1,
      };
    default:
      return MakeStatus<TPMError>("Unknown options", TPMRetryAction::kNoRetry);
  }
}

}  // namespace

StatusOr<brillo::Blob> EncryptionTpm2::Encrypt(
    Key key, const brillo::SecureBlob& plaintext, EncryptionOptions options) {
  ASSIGN_OR_RETURN(const KeyTpm2& key_data,
                   backend_.GetKeyManagementTpm2().GetKeyData(key));

  ASSIGN_OR_RETURN(const SchemaDetail& schema, GetSchemaDetail(options));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::string data = plaintext.to_string();

  // Cleanup the data from secure blob.
  base::ScopedClosureRunner cleanup_data(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(data)));

  std::string tpm_ciphertext;

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->AsymmetricEncrypt(
                      key_data.key_handle, schema.schema, schema.hash_alg, data,
                      nullptr, &tpm_ciphertext)))
      .WithStatus<TPMError>("Failed to encrypt plaintext");

  return BlobFromString(tpm_ciphertext);
}

StatusOr<brillo::SecureBlob> EncryptionTpm2::Decrypt(
    Key key, const brillo::Blob& ciphertext, EncryptionOptions options) {
  ASSIGN_OR_RETURN(const KeyTpm2& key_data,
                   backend_.GetKeyManagementTpm2().GetKeyData(key));

  ASSIGN_OR_RETURN(const SchemaDetail& schema, GetSchemaDetail(options));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");

  std::string tpm_plaintext;

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context.tpm_utility->AsymmetricDecrypt(
          key_data.key_handle, schema.schema, schema.hash_alg,
          BlobToString(ciphertext), delegate.get(), &tpm_plaintext)))
      .WithStatus<TPMError>("Failed to decrypt ciphertext");

  brillo::SecureBlob result(tpm_plaintext.begin(), tpm_plaintext.end());
  return result;
}

}  // namespace hwsec
