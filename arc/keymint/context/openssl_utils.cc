// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/openssl_utils.h"

#include <base/check_op.h>
#include <base/stl_util.h>
#include <openssl/aes.h>
#include <openssl/rand.h>

#include <optional>
#include <utility>

namespace arc::keymint::context {

namespace {

constexpr size_t kKeySize = 32;
constexpr size_t kAes256GcmPadding = 16;
constexpr uint32_t kAffinePointLength = 32;
constexpr uint32_t kSeedSize = 32;
constexpr uint32_t kP256EcdsaPrivateKeyLength = 32;

// Encrypts a given |input| using AES-GCM-256 with |key|, |auth_data|, and |iv|.
// Returns std::nullopt if there's an error in the OpenSSL operation.
std::optional<brillo::Blob> DoAes256GcmEncrypt(
    const brillo::SecureBlob& key,
    const brillo::Blob& auth_data,
    const brillo::Blob& iv,
    const brillo::SecureBlob& input) {
  CHECK_EQ(key.size(), kKeySize);
  CHECK_EQ(iv.size(), kIvSize);
  // Initialize cipher.
  crypto::ScopedEVP_CIPHER_CTX ctx(EVP_CIPHER_CTX_new());
  if (1 != EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(),
                              /* engine */ nullptr,
                              /* key */ key.data(), /* iv */ iv.data())) {
    return std::nullopt;
  }

  // Update operation with |auth_data|, out pointer must be null.
  int auth_update_len = 0;
  if (1 != EVP_EncryptUpdate(ctx.get(), /* out */ nullptr, &auth_update_len,
                             auth_data.data(), auth_data.size())) {
    return std::nullopt;
  }

  // Update operation with |input|.
  int update_len = 0;
  brillo::Blob output(input.size() + kAes256GcmPadding, 0);
  if (1 != EVP_EncryptUpdate(ctx.get(), output.data(), &update_len,
                             input.data(), input.size())) {
    return std::nullopt;
  }

  // Finish operation, accumulate results in |output|.
  int finish_len = 0;
  if (1 !=
      EVP_EncryptFinal_ex(ctx.get(), output.data() + update_len, &finish_len)) {
    return std::nullopt;
  }
  CHECK_GE(output.size(), update_len + finish_len);
  output.resize(update_len + finish_len);

  // Retrieve tag.
  brillo::Blob tag(kTagSize, 0);
  if (1 != EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, tag.size(),
                               tag.data())) {
    return std::nullopt;
  }

  // Append tag to |output| and return the encrypted blob.
  output.insert(output.end(), tag.begin(), tag.end());
  return output;
}

// Decrypts a given |input| using AES-GCM-256 with |key|, |auth_data|, and |iv|.
// Returns std::nullopt if there's an error in the OpenSSL operation.
std::optional<brillo::SecureBlob> DoAes256GcmDecrypt(
    const brillo::SecureBlob& key,
    const brillo::Blob& auth_data,
    const brillo::Blob& iv,
    const brillo::Blob& input) {
  CHECK_EQ(key.size(), kKeySize);
  CHECK_EQ(iv.size(), kIvSize);

  // Input must have a tag appended to it.
  if (input.size() < kTagSize) {
    return std::nullopt;
  }

  // Initialize cipher.
  crypto::ScopedEVP_CIPHER_CTX ctx(EVP_CIPHER_CTX_new());
  if (1 != EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(),
                              /* engine */ nullptr, key.data(), iv.data())) {
    return std::nullopt;
  }

  // Set expected tag.
  brillo::Blob tag(input.end() - kTagSize, input.end());
  size_t input_len = input.size() - tag.size();
  if (1 != EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, tag.size(),
                               tag.data())) {
    return std::nullopt;
  }

  // Update operation with |auth_data|, out pointer must be null.
  int auth_update_len = 0;
  if (1 != EVP_DecryptUpdate(ctx.get(), /* out */ nullptr, &auth_update_len,
                             auth_data.data(), auth_data.size())) {
    return std::nullopt;
  }

  // Update operation with |input|.
  int update_len = 0;
  brillo::SecureBlob output(input_len + kAes256GcmPadding, 0);
  if (1 != EVP_DecryptUpdate(ctx.get(), output.data(), &update_len,
                             input.data(), input_len)) {
    return std::nullopt;
  }

  // Finish operation, accumulate results in |output|.
  int finish_len = 0;
  if (1 !=
      EVP_CipherFinal_ex(ctx.get(), output.data() + update_len, &finish_len)) {
    return std::nullopt;
  }
  CHECK_GE(output.size(), update_len + finish_len);
  output.resize(update_len + finish_len);

  // Return decrypted blob;
  return output;
}

keymaster_error_t extractEcdsaPEMKey(bssl::UniquePtr<EC_KEY> ec_key,
                                     std::string& private_key_pem) {
  // Convert EC_KEY to EVP_PKEY
  bssl::UniquePtr<EVP_PKEY> evp_pkey(EVP_PKEY_new());
  if (!evp_pkey.get()) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  // This function will take ownership of |ec_key|.
  if (EVP_PKEY_assign_EC_KEY(evp_pkey.get(), ec_key.release()) != 1) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Convert private key to PEM format
  bssl::UniquePtr<BIO> priv_bio(BIO_new(BIO_s_mem()));
  if (!priv_bio.get()) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  if (!PEM_write_bio_PrivateKey(priv_bio.get(), evp_pkey.get(), nullptr,
                                nullptr, 0, nullptr, nullptr)) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Extract private key PEM data.
  BUF_MEM* buf;
  BIO_get_mem_ptr(priv_bio.get(), &buf);
  std::string private_key_string(buf->data, buf->length);
  private_key_pem = private_key_string;

  return KM_ERROR_OK;
}
}  // anonymous namespace

std::optional<brillo::Blob> Aes256GcmEncrypt(const brillo::SecureBlob& key,
                                             const brillo::Blob& auth_data,
                                             const brillo::SecureBlob& input) {
  // Compute a random IV.
  brillo::Blob iv(kIvSize, 0);
  if (1 != RAND_bytes(iv.data(), iv.size())) {
    return std::nullopt;
  }

  // Encrypt the input.
  std::optional<brillo::Blob> encrypted =
      DoAes256GcmEncrypt(key, auth_data, iv, input);
  if (!encrypted.has_value()) {
    return std::nullopt;
  }

  // Append the random IV used for encryption to the output.
  encrypted->insert(encrypted->end(), iv.begin(), iv.end());
  return encrypted;
}

std::optional<brillo::SecureBlob> Aes256GcmDecrypt(
    const brillo::SecureBlob& key,
    const brillo::Blob& auth_data,
    const brillo::Blob& input) {
  // Input must have an IV appended to it.
  if (input.size() < kIvSize) {
    return std::nullopt;
  }

  // Split the input between the encrypted portion and the IV.
  brillo::Blob encrypted(input.begin(), input.end() - kIvSize);
  brillo::Blob iv(input.end() - kIvSize, input.end());

  // Decrypt the input.
  return DoAes256GcmDecrypt(key, auth_data, iv, encrypted);
}

// This function is based upon AOSP Keymaster's |GetEcdsa256KeyFromCert|
// function.
keymaster_error_t GetEcdsa256KeyFromCertBlob(brillo::Blob& certData,
                                             absl::Span<uint8_t> x_coord,
                                             absl::Span<uint8_t> y_coord) {
  // Input Validation.
  if (certData.empty() || x_coord.size() != kAffinePointLength ||
      y_coord.size() != kAffinePointLength) {
    return KM_ERROR_INVALID_ARGUMENT;
  }

  // Create BIO from Vector Data.
  bssl::UniquePtr<BIO> certbio(
      BIO_new_mem_buf(certData.data(), certData.size()));
  if (!certbio.get()) {
    // Handle BIO creation error.
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Read Certificate from BIO.
  ::keymaster::X509_Ptr cert(
      PEM_read_bio_X509(certbio.get(), nullptr, nullptr, nullptr));
  if (!cert || !cert.get()) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Extract Jacobian coordinates from X509 Cert.
  ::keymaster::EVP_PKEY_Ptr pubKey(X509_get_pubkey(cert.get()));
  if (!pubKey.get()) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  EC_KEY* ecKey = EVP_PKEY_get0_EC_KEY(pubKey.get());
  if (!ecKey) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  const EC_POINT* jacobian_coords = EC_KEY_get0_public_key(ecKey);
  if (!jacobian_coords) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Extract Affine coordinates.
  bssl::UniquePtr<BIGNUM> x(BN_new());
  bssl::UniquePtr<BIGNUM> y(BN_new());
  ::keymaster::BN_CTX_Ptr ctx(BN_CTX_new());
  if (!ctx.get()) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  if (!EC_POINT_get_affine_coordinates_GFp(EC_KEY_get0_group(ecKey),
                                           jacobian_coords, x.get(), y.get(),
                                           ctx.get())) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  uint8_t* tmp_x = x_coord.data();
  if (BN_bn2binpad(x.get(), tmp_x, kAffinePointLength) != kAffinePointLength) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  uint8_t* tmp_y = y_coord.data();
  if (BN_bn2binpad(y.get(), tmp_y, kAffinePointLength) != kAffinePointLength) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  return KM_ERROR_OK;
}

keymaster_error_t GenerateEcdsa256KeyFromSeed(bool test_mode,
                                              absl::Span<uint8_t> seed,
                                              absl::Span<uint8_t> private_key,
                                              std::string& private_key_pem,
                                              absl::Span<uint8_t> x_coord,
                                              absl::Span<uint8_t> y_coord) {
  // This function is intended to work only in test mode.
  CHECK(test_mode == true);

  // Seed Input Validation.
  if (seed.size() != kSeedSize) {
    LOG(ERROR) << "Invalid seed size found";
    return KM_ERROR_INVALID_ARGUMENT;
  }

  // Size Input Validation.
  if (x_coord.size() != kAffinePointLength ||
      y_coord.size() != kAffinePointLength ||
      private_key.size() != kP256EcdsaPrivateKeyLength) {
    return KM_ERROR_INVALID_ARGUMENT;
  }

  // EC Group Setup.
  const EC_GROUP* group = nullptr;
  if (!(group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1))) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Key and Context creation.
  bssl::UniquePtr<EC_KEY> ec_key(EC_KEY_new());
  if (!ec_key.get()) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  bssl::UniquePtr<BN_CTX> ctx(BN_CTX_new());
  if (!ctx.get()) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Extract private key from seed.
  bssl::UniquePtr<BIGNUM> priv_key(
      BN_bin2bn(seed.data(), seed.size(), nullptr));
  if (!priv_key.get()) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Set Group and Private Key.
  if (EC_KEY_set_group(ec_key.get(), group) != 1) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  if (EC_KEY_set_private_key(ec_key.get(), priv_key.get()) != 1) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Calculate and Set Public Key.
  bssl::UniquePtr<EC_POINT> pub_key_point(
      EC_POINT_new(EC_KEY_get0_group(ec_key.get())));
  if (!EC_POINT_mul(EC_KEY_get0_group(ec_key.get()), pub_key_point.get(),
                    priv_key.get(), nullptr, nullptr, nullptr)) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  EC_KEY_set_public_key(ec_key.get(), pub_key_point.get());

  // Derive Public Key.
  const EC_POINT* jacobian_coords = EC_KEY_get0_public_key(ec_key.get());
  if (!jacobian_coords) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  EC_POINT* pub_key_copy = EC_POINT_new(group);
  EC_POINT_copy(pub_key_copy, jacobian_coords);
  if (EC_POINT_mul(group, pub_key_copy, priv_key.get(), nullptr, nullptr,
                   ctx.get()) != 1) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Extract Affine coordinates.
  bssl::UniquePtr<BIGNUM> x(BN_new());
  bssl::UniquePtr<BIGNUM> y(BN_new());
  if (!ctx.get()) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  if (!EC_POINT_get_affine_coordinates_GFp(EC_KEY_get0_group(ec_key.get()),
                                           jacobian_coords, x.get(), y.get(),
                                           ctx.get())) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  uint8_t* tmp_x = x_coord.data();
  if (BN_bn2binpad(x.get(), tmp_x, kAffinePointLength) != kAffinePointLength) {
    return ::keymaster::TranslateLastOpenSslError();
  }
  uint8_t* tmp_y = y_coord.data();
  if (BN_bn2binpad(y.get(), tmp_y, kAffinePointLength) != kAffinePointLength) {
    return ::keymaster::TranslateLastOpenSslError();
  }

  // Extract Private Key in PEM format.
  BN_bn2binpad(priv_key.get(), private_key.data(), private_key.size());
  auto error_pem_key = extractEcdsaPEMKey(std::move(ec_key), private_key_pem);
  if (error_pem_key != KM_ERROR_OK) {
    return error_pem_key;
  }

  return KM_ERROR_OK;
}

}  // namespace arc::keymint::context
