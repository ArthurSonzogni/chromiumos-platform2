// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/openssl_utils.h"

#include <optional>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

namespace arc::keymint::context {

namespace {

// Arbitrary 32 byte keys.
const brillo::SecureBlob kEncryptionKey1(32, 99);
const brillo::SecureBlob kEncryptionKey2(32, 98);
// Arbitrary byte arrays.
const brillo::Blob kAuthData1 = {1, 2, 3};
const brillo::Blob kAuthData2 = {1, 2, 4};
const brillo::SecureBlob kBlob(145, 42);

// Constant for Affine Size.
constexpr int kP256AffinePointSize = 32;
// Self-Signed Cert.
constexpr char kSelfSignedCert[] =
    R"(-----BEGIN CERTIFICATE-----
MIIBrjCCAVOgAwIBAgIUbCYNM3bqPXe44SH53SrutOmkzJIwCgYIKoZIzj0EAwIw
LDELMAkGA1UEBhMCWloxCzAJBgNVBAgMAkFBMRAwDgYDVQQKDAdBQkNERUZHMB4X
DTI0MDUxMzIzMjIwOVoXDTI1MDUxMzIzMjIwOVowLDELMAkGA1UEBhMCWloxCzAJ
BgNVBAgMAkFBMRAwDgYDVQQKDAdBQkNERUZHMFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAEt7gKXZh9y+UgCvjeDoXytO9hErERMsegvHHMNitjeR9+NRm/H0weE1Ld
R4m2JIiPoN6LTOQiIytmVfmtt39WSqNTMFEwHQYDVR0OBBYEFIF48JVIx+OV+Thl
pnlADPDHPsB6MB8GA1UdIwQYMBaAFIF48JVIx+OV+ThlpnlADPDHPsB6MA8GA1Ud
EwEB/wQFMAMBAf8wCgYIKoZIzj0EAwIDSQAwRgIhAOsSot6LJHwdC0gIY5Q64s4T
KSEbH8g5B1MAh0lHCxXcAiEA6CLDgUaicVdswZOPrpwBLEtm5dzA0Int2F2z/vzW
z18=
-----END CERTIFICATE-----
)";

constexpr char kBlankCert[] =
    R"(-----BEGIN CERTIFICATE-----
-----END CERTIFICATE-----
)";

// Expected Affine Keys for Cert.
// First line corresponds to X-coord and second line to Y-coord.
// This is generated from the command below
// |openssl -in cert.pem -pubkey|
constexpr char kExpectedEcdsaKey[] = R"(-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEt7gKXZh9y+UgCvjeDoXytO9hErER
MsegvHHMNitjeR9+NRm/H0weE1LdR4m2JIiPoN6LTOQiIytmVfmtt39WSg==
-----END PUBLIC KEY-----
)";

std::string generateECPublicKeyFromCoordinates(absl::Span<uint8_t> x_bytes,
                                               absl::Span<uint8_t> y_bytes) {
  // Create EC Key and Group.
  bssl::UniquePtr<EC_KEY> ec_key(
      EC_KEY_new_by_curve_name(NID_X9_62_prime256v1));  // P-256 curve

  // Create BIGNUMs from Coordinate Vectors.
  bssl::UniquePtr<BIGNUM> x(BN_bin2bn(x_bytes.data(), x_bytes.size(), NULL));
  bssl::UniquePtr<BIGNUM> y(BN_bin2bn(y_bytes.data(), y_bytes.size(), NULL));

  // Create EC Point and Set Public Key.
  bssl::UniquePtr<EC_POINT> pub_key(
      EC_POINT_new(EC_KEY_get0_group(ec_key.get())));
  EC_POINT_set_affine_coordinates_GFp(EC_KEY_get0_group(ec_key.get()),
                                      pub_key.get(), x.get(), y.get(), NULL);
  EC_KEY_set_public_key(ec_key.get(), pub_key.get());

  // Create EVP_PKEY from EC Key.
  bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new());
  EVP_PKEY_set1_EC_KEY(pkey.get(), ec_key.get());

  // Change the Key in PEM format.
  bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
  PEM_write_bio_PUBKEY(bio.get(), pkey.get());
  char* pem;
  long len = BIO_get_mem_data(bio.get(), &pem);
  std::string pem_str(pem, len);

  return pem_str;
}
}  // anonymous namespace

TEST(OpenSslUtils, EncryptThenDecrypt) {
  // Encrypt.
  std::optional<brillo::Blob> encrypted =
      Aes256GcmEncrypt(kEncryptionKey1, kAuthData1, kBlob);
  ASSERT_TRUE(encrypted.has_value());

  // Decrypt.
  std::optional<brillo::SecureBlob> decrypted =
      Aes256GcmDecrypt(kEncryptionKey1, kAuthData1, encrypted.value());
  ASSERT_TRUE(decrypted.has_value());

  // Verify blobs before encryption and after decryption match.
  ASSERT_EQ(kBlob, decrypted.value());
}

TEST(OpenSslUtils, EncryptedBlobSize) {
  // Encrypt.
  std::optional<brillo::Blob> encrypted =
      Aes256GcmEncrypt(kEncryptionKey1, kAuthData1, kBlob);
  ASSERT_TRUE(encrypted.has_value());

  // Verify encrypted blob is large enough to contain auth tag and IV.
  EXPECT_GE(encrypted->size(), kBlob.size() + kTagSize + kIvSize);
}

TEST(OpenSslUtils, DecryptWithDifferentEncryptionKeyError) {
  // Encrypt with some encryption key.
  std::optional<brillo::Blob> encrypted =
      Aes256GcmEncrypt(kEncryptionKey1, kAuthData1, kBlob);
  ASSERT_TRUE(encrypted.has_value());

  // Try to decrypt with another encryption key.
  ASSERT_NE(kEncryptionKey1, kEncryptionKey2);
  std::optional<brillo::SecureBlob> decrypted =
      Aes256GcmDecrypt(kEncryptionKey2, kAuthData1, encrypted.value());

  // Verify decryption fails.
  EXPECT_FALSE(decrypted.has_value());
}

TEST(OpenSslUtils, DecryptWithDifferentAuthDataError) {
  // Encrypt with some auth data.
  std::optional<brillo::Blob> encrypted =
      Aes256GcmEncrypt(kEncryptionKey1, kAuthData1, kBlob);
  ASSERT_TRUE(encrypted.has_value());

  // Try to decrypt with different auth data.
  ASSERT_NE(kAuthData1, kAuthData2);
  std::optional<brillo::SecureBlob> decrypted =
      Aes256GcmDecrypt(kEncryptionKey1, kAuthData2, encrypted.value());

  // Verify decryption fails.
  EXPECT_FALSE(decrypted.has_value());
}

TEST(OpenSslUtils, GetEcdsaKeyFromCertSuccess) {
  // Prepare.
  brillo::Blob certData = brillo::BlobFromString(kSelfSignedCert);

  std::vector<uint8_t> x_vect(kP256AffinePointSize);
  std::vector<uint8_t> y_vect(kP256AffinePointSize);
  absl::Span<uint8_t> x_coord(x_vect);
  absl::Span<uint8_t> y_coord(y_vect);

  // Execute.
  auto error = GetEcdsa256KeyFromCertBlob(certData, x_coord, y_coord);
  auto public_key = generateECPublicKeyFromCoordinates(x_coord, y_coord);

  // Test.
  EXPECT_EQ(error, KM_ERROR_OK);
  EXPECT_EQ(public_key, kExpectedEcdsaKey);
}

TEST(OpenSslUtils, GetEcdsaKeyFromCertFailure) {
  // Prepare.
  brillo::Blob certData = brillo::BlobFromString(kBlankCert);
  std::vector<uint8_t> x_vect(kP256AffinePointSize);
  std::vector<uint8_t> y_vect(kP256AffinePointSize);
  absl::Span<uint8_t> x_coord(x_vect);
  absl::Span<uint8_t> y_coord(y_vect);

  // Execute.
  auto error = GetEcdsa256KeyFromCertBlob(certData, x_coord, y_coord);
  auto public_key = generateECPublicKeyFromCoordinates(x_coord, y_coord);

  // Test.
  ASSERT_FALSE(error == KM_ERROR_OK);
}
}  // namespace arc::keymint::context
