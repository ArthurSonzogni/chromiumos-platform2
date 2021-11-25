// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/passpoint_credentials.h"

#include <limits>
#include <string>
#include <vector>

#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/error.h"
#include "shill/key_value_store.h"
#include "shill/profile.h"
#include "shill/refptr_types.h"
#include "shill/supplicant/wpa_supplicant.h"

namespace shill {

namespace {
std::vector<std::string> toStringList(std::vector<uint64_t> list) {
  std::vector<std::string> out;
  for (uint64_t value : list) {
    out.push_back(base::NumberToString(value));
  }
  return out;
}
}  // namespace

class PasspointCredentialsTest : public ::testing::Test {
 public:
  PasspointCredentialsTest() = default;
  ~PasspointCredentialsTest() override = default;
};

TEST_F(PasspointCredentialsTest, CreateChecksMatchDomains) {
  const std::string kValidFQDN1("example.com");
  const std::string kValidFQDN2("example.net");
  const Strings kValidFQDNs{kValidFQDN1, kValidFQDN2};
  const std::string kInvalidDomain("-foo.com");
  const Strings kInvalidDomains{kInvalidDomain};
  const std::string kUser("test-user");
  const std::string kPassword("test-password");
  KeyValueStore properties;
  Error error;

  // No domain fails
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // Invalid domain fails
  error.Reset();
  properties.Set(kPasspointCredentialsDomainsProperty, kInvalidDomains);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // No realm or invalid realm fails
  error.Reset();
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // Invalid realm fails
  error.Reset();
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kInvalidDomain);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);
}

TEST_F(PasspointCredentialsTest, CreateChecksEapCredentials) {
  const std::string kValidFQDN("example.com");
  const Strings kValidFQDNs{kValidFQDN};
  const std::string kInvalidDomain("-bar.com");
  const std::string kUser("test-user");
  const std::string kPassword("test-password");
  const std::string kMethodTTLS(kEapMethodTTLS);
  const std::string kSubjectNameMatch("domain1.com");
  const std::vector<std::string> kCaCertPem{"pem first line",
                                            "pem second line"};
  const std::vector<std::string> kAlternativeNameMatchList{"domain2.com",
                                                           "domain3.com"};
  const std::vector<std::string> kDomainSuffixMatchList{"domain4.com",
                                                        "domain5.com"};
  const std::vector<std::string> kInvalidOis{"1122", "notanumber"};
  KeyValueStore properties;
  Error error;

  // No EAP credentials fails.
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // Invalid EAP method
  error.Reset();
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kEapCaCertPemProperty, kCaCertPem);
  // The following properties are enough to create a connectable EAP set.
  properties.Set(kEapMethodProperty, std::string(kEapMethodPEAP));
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // Invalid inner EAP method with TTLS
  error.Reset();
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kEapCaCertPemProperty, kCaCertPem);
  properties.Set(kEapMethodProperty, kMethodTTLS);
  properties.Set(kEapPhase2AuthProperty, std::string(kEapPhase2AuthTTLSMD5));
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // No CA cert and only a subject name match.
  error.Reset();
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kEapMethodProperty, kMethodTTLS);
  properties.Set(kEapPhase2AuthProperty,
                 std::string(kEapPhase2AuthTTLSMSCHAPV2));
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  properties.Set(kEapSubjectMatchProperty, kSubjectNameMatch);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // No CA cert and only a domain suffix name match list
  error.Reset();
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kEapMethodProperty, kMethodTTLS);
  properties.Set(kEapPhase2AuthProperty,
                 std::string(kEapPhase2AuthTTLSMSCHAPV2));
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  properties.Set(kEapDomainSuffixMatchProperty, kDomainSuffixMatchList);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // Incorrect home OIs
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kPasspointCredentialsHomeOIsProperty, kInvalidOis);
  properties.Set(kEapMethodProperty, kMethodTTLS);
  properties.Set(kEapPhase2AuthProperty,
                 std::string(kEapPhase2AuthTTLSMSCHAPV2));
  properties.Set(kEapCaCertPemProperty, kCaCertPem);
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // Incorrect required home OIs
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kPasspointCredentialsRequiredHomeOIsProperty, kInvalidOis);
  properties.Set(kEapMethodProperty, kMethodTTLS);
  properties.Set(kEapPhase2AuthProperty,
                 std::string(kEapPhase2AuthTTLSMSCHAPV2));
  properties.Set(kEapCaCertPemProperty, kCaCertPem);
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);

  // Incorrect roaming consortia OIs
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kPasspointCredentialsRoamingConsortiaProperty, kInvalidOis);
  properties.Set(kEapMethodProperty, kMethodTTLS);
  properties.Set(kEapPhase2AuthProperty,
                 std::string(kEapPhase2AuthTTLSMSCHAPV2));
  properties.Set(kEapCaCertPemProperty, kCaCertPem);
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  EXPECT_EQ(
      PasspointCredentials::CreatePasspointCredentials(properties, &error),
      nullptr);
  EXPECT_EQ(error.type(), Error::kInvalidArguments);
}

TEST_F(PasspointCredentialsTest, Create) {
  const std::string kValidFQDN("example.com");
  const Strings kValidFQDNs{kValidFQDN};
  const std::string kInvalidDomain("-abc.com");
  const std::string kUser("test-user");
  const std::string kPassword("test-password");
  const std::string kMethodTLS(kEapMethodTLS);
  const std::string kMethodTTLS(kEapMethodTTLS);
  const std::vector<uint64_t> kOIs{0x123456789, 0x1045985432,
                                   std::numeric_limits<uint64_t>::min(),
                                   std::numeric_limits<uint64_t>::max()};
  const std::vector<uint64_t> kRoamingConsortia{123456789, 321645987,
                                                9876453120};
  const std::vector<std::string> kCaCertPem{"pem first line",
                                            "pem second line"};
  const std::string kPackageName("com.foo.bar");
  const std::string kCertId("cert-id");
  const std::string kKeyId("key-id");
  const std::string kSubjectNameMatch("domain1.com");
  const std::vector<std::string> kAlternativeNameMatchList{"domain2.com",
                                                           "domain3.com"};
  const std::vector<std::string> kDomainSuffixMatchList{"domain4.com",
                                                        "domain5.com"};

  KeyValueStore properties;
  Error error;

  // Verify Passpoint+EAP-TLS with CA cert
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kPasspointCredentialsHomeOIsProperty, toStringList(kOIs));
  properties.Set(kPasspointCredentialsRequiredHomeOIsProperty,
                 toStringList(kOIs));
  properties.Set(kPasspointCredentialsRoamingConsortiaProperty,
                 toStringList(kRoamingConsortia));
  properties.Set(kPasspointCredentialsMeteredOverrideProperty, true);
  properties.Set(kPasspointCredentialsAndroidPackageNameProperty, kPackageName);
  properties.Set(kEapMethodProperty, kMethodTLS);
  properties.Set(kEapCaCertPemProperty, kCaCertPem);
  properties.Set(kEapCertIdProperty, kCertId);
  properties.Set(kEapKeyIdProperty, kKeyId);
  properties.Set(kEapPinProperty, std::string("111111"));
  properties.Set(kEapIdentityProperty, kUser);

  PasspointCredentialsRefPtr creds =
      PasspointCredentials::CreatePasspointCredentials(properties, &error);

  EXPECT_NE(nullptr, creds);
  EXPECT_EQ(kValidFQDNs, creds->domains());
  EXPECT_EQ(kValidFQDN, creds->realm());
  EXPECT_EQ(kOIs, creds->home_ois());
  EXPECT_EQ(kOIs, creds->required_home_ois());
  EXPECT_EQ(kRoamingConsortia, creds->roaming_consortia());
  EXPECT_TRUE(creds->metered_override());
  EXPECT_EQ(kPackageName, creds->android_package_name());
  EXPECT_TRUE(creds->eap().IsConnectable());
  EXPECT_FALSE(creds->eap().use_system_cas());

  // Verify Passpoint+EAP-TTLS with CA cert
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kPasspointCredentialsHomeOIsProperty, toStringList(kOIs));
  properties.Set(kPasspointCredentialsRequiredHomeOIsProperty,
                 toStringList(kOIs));
  properties.Set(kPasspointCredentialsRoamingConsortiaProperty,
                 toStringList(kRoamingConsortia));
  properties.Set(kPasspointCredentialsMeteredOverrideProperty, true);
  properties.Set(kPasspointCredentialsAndroidPackageNameProperty, kPackageName);
  properties.Set(kEapMethodProperty, kMethodTTLS);
  properties.Set(kEapPhase2AuthProperty,
                 std::string(kEapPhase2AuthTTLSMSCHAPV2));
  properties.Set(kEapCaCertPemProperty, kCaCertPem);
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  EXPECT_FALSE(creds->eap().use_system_cas());

  creds = PasspointCredentials::CreatePasspointCredentials(properties, &error);
  EXPECT_NE(nullptr, creds);
  EXPECT_EQ(kValidFQDNs, creds->domains());
  EXPECT_EQ(kValidFQDN, creds->realm());
  EXPECT_EQ(kOIs, creds->home_ois());
  EXPECT_EQ(kOIs, creds->required_home_ois());
  EXPECT_EQ(kRoamingConsortia, creds->roaming_consortia());
  EXPECT_TRUE(creds->metered_override());
  EXPECT_EQ(kPackageName, creds->android_package_name());
  EXPECT_TRUE(creds->eap().IsConnectable());

  // Verify Passpoint+EAP-TTLS without CA cert and with altname match list
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kPasspointCredentialsHomeOIsProperty, toStringList(kOIs));
  properties.Set(kPasspointCredentialsRequiredHomeOIsProperty,
                 toStringList(kOIs));
  properties.Set(kPasspointCredentialsRoamingConsortiaProperty,
                 toStringList(kRoamingConsortia));
  properties.Set(kPasspointCredentialsMeteredOverrideProperty, true);
  properties.Set(kPasspointCredentialsAndroidPackageNameProperty, kPackageName);
  properties.Set(kEapMethodProperty, kMethodTTLS);
  properties.Set(kEapPhase2AuthProperty,
                 std::string(kEapPhase2AuthTTLSMSCHAPV2));
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  properties.Set(kEapSubjectAlternativeNameMatchProperty,
                 kAlternativeNameMatchList);

  creds = PasspointCredentials::CreatePasspointCredentials(properties, &error);
  EXPECT_NE(nullptr, creds);
  EXPECT_EQ(kValidFQDNs, creds->domains());
  EXPECT_EQ(kValidFQDN, creds->realm());
  EXPECT_EQ(kOIs, creds->home_ois());
  EXPECT_EQ(kOIs, creds->required_home_ois());
  EXPECT_EQ(kRoamingConsortia, creds->roaming_consortia());
  EXPECT_TRUE(creds->metered_override());
  EXPECT_EQ(kPackageName, creds->android_package_name());
  EXPECT_TRUE(creds->eap().IsConnectable());
  EXPECT_TRUE(creds->eap().use_system_cas());

  // Verify Passpoint+EAP-TTLS without CA cert and with domain suffix name match
  // list
  properties.Clear();
  properties.Set(kPasspointCredentialsDomainsProperty, kValidFQDNs);
  properties.Set(kPasspointCredentialsRealmProperty, kValidFQDN);
  properties.Set(kPasspointCredentialsHomeOIsProperty, toStringList(kOIs));
  properties.Set(kPasspointCredentialsRequiredHomeOIsProperty,
                 toStringList(kOIs));
  properties.Set(kPasspointCredentialsRoamingConsortiaProperty,
                 toStringList(kRoamingConsortia));
  properties.Set(kPasspointCredentialsMeteredOverrideProperty, true);
  properties.Set(kPasspointCredentialsAndroidPackageNameProperty, kPackageName);
  properties.Set(kEapMethodProperty, kMethodTTLS);
  properties.Set(kEapPhase2AuthProperty,
                 std::string(kEapPhase2AuthTTLSMSCHAPV2));
  properties.Set(kEapIdentityProperty, kUser);
  properties.Set(kEapPasswordProperty, kPassword);
  properties.Set(kEapSubjectMatchProperty, kSubjectNameMatch);
  properties.Set(kEapDomainSuffixMatchProperty, kDomainSuffixMatchList);

  creds = PasspointCredentials::CreatePasspointCredentials(properties, &error);
  EXPECT_NE(nullptr, creds);
  EXPECT_EQ(kValidFQDNs, creds->domains());
  EXPECT_EQ(kValidFQDN, creds->realm());
  EXPECT_EQ(kOIs, creds->home_ois());
  EXPECT_EQ(kOIs, creds->required_home_ois());
  EXPECT_EQ(kRoamingConsortia, creds->roaming_consortia());
  EXPECT_TRUE(creds->metered_override());
  EXPECT_EQ(kPackageName, creds->android_package_name());
  EXPECT_TRUE(creds->eap().IsConnectable());
  EXPECT_TRUE(creds->eap().use_system_cas());
}

TEST_F(PasspointCredentialsTest, ToSupplicantProperties) {
  const std::vector<std::string> domains{"blue-sp.example.com",
                                         "green-sp.example.com"};
  const std::string realm("blue-sp.example.com");
  const std::vector<uint64_t> home_ois{0x1234, 0x5678};
  const std::vector<uint64_t> required_home_ois{0xabcd, 0xcdef};
  const std::vector<uint64_t> roaming_consortia{0x11111111, 0x22222222};

  PasspointCredentialsRefPtr creds = new PasspointCredentials(
      "an_id", domains, realm, home_ois, required_home_ois, roaming_consortia,
      /*metered_override=*/false, "app_package_name");

  KeyValueStore properties;
  creds->ToSupplicantProperties(&properties);

  EXPECT_EQ(domains[0], properties.Get<std::string>(
                            WPASupplicant::kCredentialsPropertyDomain));
  EXPECT_EQ(realm, properties.Get<std::string>(
                       WPASupplicant::kCredentialsPropertyRealm));
  // We expect the EAP method to be set, this is mandatory for supplicant to
  // perform matches. Right now the value is unknown because the EAP properties
  // can't be set with the constructor.
  EXPECT_TRUE(
      properties.Contains<std::string>(WPASupplicant::kNetworkPropertyEapEap));
  // TODO(b/162106001) check home, required home and roaming consortium OIs
}

}  // namespace shill
