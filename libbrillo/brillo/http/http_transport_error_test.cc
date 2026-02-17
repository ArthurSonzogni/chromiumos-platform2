// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/errors/error.h>
#include <brillo/http/http_transport_error.h>
#include <gtest/gtest.h>

namespace brillo::http {
namespace {

TEST(TransportErrorToStringTest, CoversAllValues) {
  constexpr TransportError kAll[] = {
      TransportError::kUnknown,
      TransportError::kDnsFailure,
      TransportError::kDnsTimeout,
      TransportError::kProxyDnsFailure,
      TransportError::kProxyConnectionFailure,
      TransportError::kConnectionFailure,
      TransportError::kTimeout,
      TransportError::kTLSFailure,
      TransportError::kCertificateError,
      TransportError::kHttpError,
      TransportError::kIOError,
      TransportError::kNetworkError,
      TransportError::kInternalError,
      TransportError::kBackendFailed,
  };
  for (auto code : kAll) {
    SCOPED_TRACE(static_cast<int>(code));
    std::string_view str = TransportErrorToString(code);
    EXPECT_FALSE(str.empty());
  }
}

TEST(ClassifyTransportErrorTest, ReturnsNulloptForNull) {
  EXPECT_EQ(ClassifyTransportError(nullptr), std::nullopt);
}

TEST(ClassifyTransportErrorTest, ReturnsNulloptForOtherDomain) {
  brillo::ErrorPtr error;
  brillo::Error::AddTo(&error, FROM_HERE, "some_domain", "42", "msg");
  EXPECT_EQ(ClassifyTransportError(error.get()), std::nullopt);
}

TEST(ClassifyTransportErrorTest, ReturnsNulloptForBadCodeString) {
  brillo::ErrorPtr error;
  brillo::Error::AddTo(&error, FROM_HERE, kTransportErrorDomain,
                       "not_a_real_code", "msg");
  EXPECT_EQ(ClassifyTransportError(error.get()), std::nullopt);
}

TEST(ClassifyTransportErrorTest, FindsEntryInChain) {
  brillo::ErrorPtr error;
  // Inner transport error.
  AddTransportError(&error, FROM_HERE, TransportError::kTimeout, "timed out");
  // Outer unrelated error.
  brillo::Error::AddTo(&error, FROM_HERE, "other_domain", "99", "msg");

  EXPECT_EQ(ClassifyTransportError(error.get()), TransportError::kTimeout);
}

TEST(AddTransportErrorTest, RoundTripsAllCodes) {
  constexpr TransportError kAll[] = {
      TransportError::kUnknown,
      TransportError::kDnsFailure,
      TransportError::kDnsTimeout,
      TransportError::kProxyDnsFailure,
      TransportError::kProxyConnectionFailure,
      TransportError::kConnectionFailure,
      TransportError::kTimeout,
      TransportError::kTLSFailure,
      TransportError::kCertificateError,
      TransportError::kHttpError,
      TransportError::kIOError,
      TransportError::kNetworkError,
      TransportError::kInternalError,
      TransportError::kBackendFailed,
  };
  for (auto code : kAll) {
    SCOPED_TRACE(TransportErrorToString(code));
    brillo::ErrorPtr error;
    AddTransportError(&error, FROM_HERE, code, "msg");
    EXPECT_EQ(ClassifyTransportError(error.get()), code);
  }
}

TEST(AddTransportErrorTest, SetsCorrectDomainAndCode) {
  brillo::ErrorPtr error;
  AddTransportError(&error, FROM_HERE, TransportError::kDnsFailure,
                    "dns failed");
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(error->GetDomain(), kTransportErrorDomain);
  EXPECT_EQ(error->GetCode(), "kDnsFailure");
  EXPECT_EQ(error->GetMessage(), "dns failed");
}

}  // namespace
}  // namespace brillo::http
