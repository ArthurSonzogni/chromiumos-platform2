// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/elliptic_curve.h"

#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/error_util.h"

#include <gtest/gtest.h>

#include <base/logging.h>

namespace cryptohome {

namespace {

constexpr EllipticCurve::CurveType kCurve = EllipticCurve::CurveType::kPrime256;
constexpr int kScalarSizeInBytes = 32;
constexpr int kFieldElementSizeInBytes = 32;

// Creates secure blob in a uncompressed point format (see ANSI X9.62
// standard), but not on a curve.
brillo::SecureBlob CreateBogusPointBlob() {
  // Curve size is 256bit = 32bytes, so uncompressed point format is:
  //   0x04  (header)
  //   X (32 bytes)
  //   Y (32 bytes)
  // Total size = 1 + 32 + 32 = 65 bytes.
  char buf[66];
  buf[0] = 0x04;  // Set header.
  for (int i = 1; i < 65; ++i)
    buf[i] = i;
  buf[65] = 0;  // Terminate with zero as a string.
  return brillo::SecureBlob(buf);
}

}  // namespace

class EllipticCurveTest : public testing::Test {
 public:
  void SetUp() override {
    context_ = CreateBigNumContext();
    ASSERT_TRUE(context_);
    ec_ = EllipticCurve::Create(kCurve, context_.get());
    ASSERT_TRUE(ec_);
  }

  // Creates point as generator multiplied by scalar_value.
  // Returns nullptr if error occurred.
  crypto::ScopedEC_POINT CreatePoint(BN_ULONG scalar_value) {
    crypto::ScopedBIGNUM scalar = BigNumFromValue(scalar_value);
    if (!scalar) {
      LOG(ERROR) << "Failed to create BIGNUM structure";
      return nullptr;
    }
    return ec_->MultiplyWithGenerator(*scalar, context_.get());
  }

  // Creates invalid point that is not on a curve.
  // Returns nullptr if error occurred.
  crypto::ScopedEC_POINT CreateInvalidPoint() {
    crypto::ScopedEC_POINT point = ec_->PointAtInfinityForTesting();
    if (!point) {
      LOG(ERROR) << "Failed to create point at infinity";
      return nullptr;
    }

    // Set point to some coordinates that are not on a curve.
    crypto::ScopedBIGNUM x = BigNumFromValue(123u);
    if (!x) {
      LOG(ERROR) << "Failed to create BIGNUM structure";
      return nullptr;
    }
    crypto::ScopedBIGNUM y = BigNumFromValue(321u);
    if (!y) {
      LOG(ERROR) << "Failed to create BIGNUM structure";
      return nullptr;
    }

    // Set affine coordinates outside of the curve. Assume the method will fail,
    // but it should still initialize the point.
    if (EC_POINT_set_affine_coordinates(ec_->GetGroupForTesting(), point.get(),
                                        x.get(), y.get(),
                                        context_.get()) == 1) {
      LOG(ERROR) << "Failed to set affine coords for invalid point";
      return nullptr;
    }

    // Capture OpenSSL error from error stack.
    std::string error = GetOpenSSLErrors();
    if (error.find("EC_POINT_set_affine_coordinates:point is not on curve") ==
        std::string::npos) {
      LOG(ERROR) << "Failed to create invalid point";
      return nullptr;
    }

    // Verify that the point is not at infinity anymore, so it was indeed set,
    // but it's not on a curve.
    if (ec_->IsPointAtInfinity(*point) ||
        ec_->IsPointValid(*point, context_.get())) {
      LOG(ERROR) << "Failed to create invalid point";
      return nullptr;
    }
    return point;
  }

 protected:
  ScopedBN_CTX context_;
  base::Optional<EllipticCurve> ec_;
};

TEST_F(EllipticCurveTest, ScalarAndFieldSizeInBytes) {
  EXPECT_EQ(ec_->ScalarSizeInBytes(), kScalarSizeInBytes);
  EXPECT_EQ(ec_->FieldElementSizeInBytes(), kFieldElementSizeInBytes);
}

TEST_F(EllipticCurveTest, PointAtInfinity) {
  crypto::ScopedEC_POINT point = ec_->PointAtInfinityForTesting();
  ASSERT_TRUE(point);
  EXPECT_TRUE(ec_->IsPointValid(*point, context_.get()));
  EXPECT_TRUE(ec_->IsPointAtInfinity(*point));
}

TEST_F(EllipticCurveTest, RandomNonZeroScalar) {
  // Generates random secret. Note that this is non-deterministic,
  // so we just check if the output is smaller than curve order
  // and non-zero.
  crypto::ScopedBIGNUM secret = ec_->RandomNonZeroScalar(context_.get());
  ASSERT_TRUE(secret);
  EXPECT_EQ(BN_cmp(secret.get(), ec_->GetOrderForTesting()), -1);
  EXPECT_EQ(BN_is_zero(secret.get()), 0);
}

TEST_F(EllipticCurveTest, SecureBlobConversions) {
  crypto::ScopedEC_POINT point = CreatePoint(123u);
  ASSERT_TRUE(point);
  brillo::SecureBlob point_blob;
  ASSERT_TRUE(ec_->PointToSecureBlob(*point, &point_blob, context_.get()));
  crypto::ScopedEC_POINT point2 =
      ec_->SecureBlobToPoint(point_blob, context_.get());
  ASSERT_TRUE(point2);
  EXPECT_TRUE(ec_->AreEqual(*point, *point2, context_.get()));

  // Test conversions of invalid or infinite points.
  point = CreateInvalidPoint();
  ASSERT_TRUE(point);
  EXPECT_FALSE(ec_->PointToSecureBlob(*point, &point_blob, context_.get()));
  point = ec_->PointAtInfinityForTesting();
  ASSERT_TRUE(point);
  EXPECT_FALSE(ec_->PointToSecureBlob(*point, &point_blob, context_.get()));
  point_blob = CreateBogusPointBlob();
  EXPECT_FALSE(ec_->SecureBlobToPoint(point_blob, context_.get()));
}

TEST_F(EllipticCurveTest, Add) {
  crypto::ScopedEC_POINT point1 = CreatePoint(1u);
  ASSERT_TRUE(point1);
  crypto::ScopedEC_POINT point2 = CreatePoint(2u);
  ASSERT_TRUE(point2);
  crypto::ScopedEC_POINT point3 = CreatePoint(3u);
  ASSERT_TRUE(point3);

  crypto::ScopedEC_POINT result = ec_->Add(*point1, *point2, context_.get());
  ASSERT_TRUE(result);
  EXPECT_TRUE(ec_->AreEqual(*result, *point3, context_.get()));

  // Double the point.
  result = ec_->Add(*point1, *point1, context_.get());
  ASSERT_TRUE(result);
  EXPECT_TRUE(ec_->AreEqual(*result, *point2, context_.get()));

  // Add point to its inverse.
  crypto::ScopedEC_POINT inv_point3 = CreatePoint(3u);
  ASSERT_EQ(EC_POINT_invert(ec_->GetGroupForTesting(), inv_point3.get(),
                            context_.get()),
            1);
  result = ec_->Add(*point3, *inv_point3, context_.get());
  ASSERT_TRUE(result);
  EXPECT_TRUE(ec_->IsPointAtInfinity(*result));

  // Check if inverse of nG is (order-n)*G.
  crypto::ScopedBIGNUM order_sub_3 = BigNumFromValue(3u);
  ASSERT_TRUE(order_sub_3);
  ASSERT_EQ(
      BN_sub(order_sub_3.get(), ec_->GetOrderForTesting(), order_sub_3.get()),
      1);
  result = ec_->MultiplyWithGenerator(*order_sub_3, context_.get());
  EXPECT_TRUE(ec_->AreEqual(*inv_point3, *result, context_.get()));

  // Double point at infinity.
  crypto::ScopedEC_POINT point_at_inf = ec_->PointAtInfinityForTesting();
  result = ec_->Add(*point_at_inf, *point_at_inf, context_.get());
  ASSERT_TRUE(result);
  EXPECT_TRUE(ec_->IsPointAtInfinity(*point_at_inf));
}

TEST_F(EllipticCurveTest, MultiplicationWithGenerator) {
  crypto::ScopedBIGNUM scalar1 = BigNumFromValue(123u);
  ASSERT_TRUE(scalar1);
  crypto::ScopedBIGNUM scalar2 = BigNumFromValue(321u);
  ASSERT_TRUE(scalar2);
  crypto::ScopedBIGNUM scalar_prod = CreateBigNum();
  ASSERT_TRUE(scalar_prod);
  ASSERT_EQ(
      BN_mul(scalar_prod.get(), scalar1.get(), scalar2.get(), context_.get()),
      1);
  EXPECT_EQ(BN_get_word(scalar_prod.get()), 123u * 321u);

  // Test if (G*scalar1)*scalar2 = G*(scalar1*scalar2).
  crypto::ScopedEC_POINT point1 =
      ec_->MultiplyWithGenerator(*scalar1, context_.get());
  EXPECT_TRUE(ec_->IsPointValidAndFinite(*point1, context_.get()));
  crypto::ScopedEC_POINT point2 =
      ec_->Multiply(*point1, *scalar2, context_.get());
  EXPECT_TRUE(ec_->IsPointValidAndFinite(*point2, context_.get()));
  crypto::ScopedEC_POINT point_prod =
      ec_->MultiplyWithGenerator(*scalar_prod, context_.get());
  EXPECT_TRUE(ec_->IsPointValidAndFinite(*point_prod, context_.get()));
  EXPECT_TRUE(ec_->AreEqual(*point2, *point_prod, context_.get()));
}

TEST_F(EllipticCurveTest, MultiplyWithGeneratorByBigScalars) {
  // Get big scalars of curve order.
  crypto::ScopedBIGNUM scalar1 = BigNumFromValue(123u);
  ASSERT_TRUE(scalar1);
  ASSERT_EQ(BN_sub(scalar1.get(), ec_->GetOrderForTesting(), scalar1.get()), 1);
  crypto::ScopedBIGNUM scalar2 = BigNumFromValue(321u);
  ASSERT_TRUE(scalar2);
  ASSERT_EQ(BN_sub(scalar2.get(), ec_->GetOrderForTesting(), scalar2.get()), 1);

  crypto::ScopedBIGNUM scalar_sum = CreateBigNum();
  ASSERT_TRUE(scalar_sum);
  ASSERT_EQ(BN_add(scalar_sum.get(), scalar1.get(), scalar2.get()), 1);
  // Expect scalar_sum > order.
  EXPECT_EQ(BN_cmp(scalar_sum.get(), ec_->GetOrderForTesting()), 1);
  // Multiplication by scalar greater than order should fail.
  EXPECT_FALSE(ec_->MultiplyWithGenerator(*scalar_sum, context_.get()));

  crypto::ScopedBIGNUM scalar_mod_sum =
      ec_->ModAdd(*scalar1, *scalar2, context_.get());
  ASSERT_TRUE(scalar_mod_sum);
  // Expect scalar_mod_sum < order.
  EXPECT_EQ(BN_cmp(scalar_mod_sum.get(), ec_->GetOrderForTesting()), -1);

  // Test if G*scalar1 + G*scalar2 = G*((scalar1 + scalar2) mod order).
  crypto::ScopedEC_POINT point1 =
      ec_->MultiplyWithGenerator(*scalar1, context_.get());
  ASSERT_TRUE(point1);
  EXPECT_TRUE(ec_->IsPointValidAndFinite(*point1, context_.get()));
  crypto::ScopedEC_POINT point2 =
      ec_->MultiplyWithGenerator(*scalar2, context_.get());
  ASSERT_TRUE(point2);
  EXPECT_TRUE(ec_->IsPointValidAndFinite(*point2, context_.get()));
  crypto::ScopedEC_POINT point_sum1 =
      ec_->MultiplyWithGenerator(*scalar_mod_sum, context_.get());
  ASSERT_TRUE(point_sum1);
  EXPECT_TRUE(ec_->IsPointValidAndFinite(*point_sum1, context_.get()));
  crypto::ScopedEC_POINT point_sum2 =
      ec_->Add(*point1, *point2, context_.get());
  ASSERT_TRUE(point_sum2);
  EXPECT_TRUE(ec_->IsPointValidAndFinite(*point_sum2, context_.get()));
  EXPECT_TRUE(ec_->AreEqual(*point_sum1, *point_sum2, context_.get()));
}

TEST_F(EllipticCurveTest, MultiplyWithGeneratorByZero) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(0);
  crypto::ScopedEC_POINT point =
      ec_->MultiplyWithGenerator(*scalar, context_.get());
  EXPECT_TRUE(ec_->IsPointValid(*point, context_.get()));
  EXPECT_TRUE(ec_->IsPointAtInfinity(*point));
}

TEST_F(EllipticCurveTest, MultiplyWithPointAtInfinity) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123u);
  ASSERT_TRUE(scalar);
  crypto::ScopedEC_POINT point = ec_->PointAtInfinityForTesting();
  ASSERT_TRUE(point);

  crypto::ScopedEC_POINT result =
      ec_->Multiply(*point, *scalar, context_.get());
  ASSERT_TRUE(result);
  EXPECT_TRUE(ec_->IsPointAtInfinity(*result));

  // Try 0 x point at infinity. The result should be also point at infinity.
  scalar = BigNumFromValue(0u);
  ASSERT_TRUE(scalar);
  result = ec_->Multiply(*point, *scalar, context_.get());
  ASSERT_TRUE(result);
  EXPECT_TRUE(ec_->IsPointAtInfinity(*result));
}

TEST_F(EllipticCurveTest, MultiplyWithInvalidPoint) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(1u);
  ASSERT_TRUE(scalar);
  crypto::ScopedEC_POINT point = CreateInvalidPoint();
  ASSERT_TRUE(point);

  // Verify that multiplication does not accept bogus point as the input.
  crypto::ScopedEC_POINT result =
      ec_->Multiply(*point, *scalar, context_.get());
  EXPECT_FALSE(result);
}

TEST_F(EllipticCurveTest, MultiplyWithGeneratorByNegative) {
  crypto::ScopedBIGNUM scalar1 = BigNumFromValue(123u);
  ASSERT_TRUE(scalar1);
  crypto::ScopedBIGNUM scalar2 = BigNumFromValue(321u);
  ASSERT_TRUE(scalar2);

  crypto::ScopedEC_POINT point1 =
      ec_->MultiplyWithGenerator(*scalar1, context_.get());
  crypto::ScopedEC_POINT point2 =
      ec_->MultiplyWithGenerator(*scalar2, context_.get());
  BN_set_negative(scalar1.get(), 1);
  crypto::ScopedEC_POINT inverse_point1 =
      ec_->MultiplyWithGenerator(*scalar1, context_.get());

  crypto::ScopedEC_POINT point_sum_12 =
      ec_->Add(*point1, *point2, context_.get());
  crypto::ScopedEC_POINT point_sum_all =
      ec_->Add(*point_sum_12, *inverse_point1, context_.get());
  // Validates that after adding the inversion of point1 its contribution
  // cancels out and we are left with point2.
  ASSERT_TRUE(ec_->AreEqual(*point2, *point_sum_all, context_.get()));
}

TEST_F(EllipticCurveTest, GenerateKey) {
  crypto::ScopedEC_KEY key = ec_->GenerateKey(context_.get());
  ASSERT_TRUE(key);
  const BIGNUM* private_key = EC_KEY_get0_private_key(key.get());
  ASSERT_TRUE(private_key);
  const EC_POINT* public_key = EC_KEY_get0_public_key(key.get());
  ASSERT_TRUE(public_key);

  // Validate that private_key * G = public_key.
  crypto::ScopedEC_POINT expected_public_key =
      ec_->MultiplyWithGenerator(*private_key, context_.get());
  EXPECT_TRUE(ec_->AreEqual(*expected_public_key, *public_key, context_.get()));
}

TEST_F(EllipticCurveTest, GenerateKeysAsSecureBlobs) {
  brillo::SecureBlob public_blob;
  brillo::SecureBlob private_blob;
  ASSERT_TRUE(ec_->GenerateKeysAsSecureBlobs(&public_blob, &private_blob,
                                             context_.get()));

  crypto::ScopedEC_POINT public_key =
      ec_->SecureBlobToPoint(public_blob, context_.get());
  ASSERT_TRUE(public_key);
  crypto::ScopedBIGNUM private_key = SecureBlobToBigNum(private_blob);
  ASSERT_TRUE(private_key);

  // Validate that private_key * G = public_key.
  crypto::ScopedEC_POINT expected_public_key =
      ec_->MultiplyWithGenerator(*private_key, context_.get());
  EXPECT_TRUE(ec_->AreEqual(*expected_public_key, *public_key, context_.get()));
}

TEST_F(EllipticCurveTest, InvertPoint) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123u);
  ASSERT_TRUE(scalar);
  crypto::ScopedEC_POINT point =
      ec_->MultiplyWithGenerator(*scalar, context_.get());

  BN_set_negative(scalar.get(), 1);
  crypto::ScopedEC_POINT inverse_point =
      ec_->MultiplyWithGenerator(*scalar, context_.get());

  EXPECT_TRUE(ec_->InvertPoint(point.get(), context_.get()));

  // Validates that the inverted point equals to inverse_point.
  EXPECT_TRUE(ec_->AreEqual(*inverse_point, *point, context_.get()));
}

TEST_F(EllipticCurveTest, InversePointAddition) {
  crypto::ScopedBIGNUM scalar1 = BigNumFromValue(123u);
  ASSERT_TRUE(scalar1);
  crypto::ScopedBIGNUM scalar2 = BigNumFromValue(321u);
  ASSERT_TRUE(scalar2);

  crypto::ScopedEC_POINT point1 =
      ec_->MultiplyWithGenerator(*scalar1, context_.get());
  ASSERT_TRUE(point1);
  crypto::ScopedEC_POINT point2 =
      ec_->MultiplyWithGenerator(*scalar2, context_.get());
  ASSERT_TRUE(point2);
  crypto::ScopedEC_POINT point_sum_12 =
      ec_->Add(*point1, *point2, context_.get());
  ASSERT_TRUE(point_sum_12);

  ec_->InvertPoint(point1.get(), context_.get());
  crypto::ScopedEC_POINT point_sum_all =
      ec_->Add(*point_sum_12, *point1, context_.get());
  ASSERT_TRUE(point_sum_all);
  // Validates that after adding the inverted point1 its contribution
  // cancels out and we are left with point2.
  EXPECT_TRUE(ec_->AreEqual(*point2, *point_sum_all, context_.get()));
}

}  // namespace cryptohome
