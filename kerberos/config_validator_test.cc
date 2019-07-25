// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "kerberos/config_validator.h"

#include <ostream>
#include <string>

#include <base/logging.h>
#include <gtest/gtest.h>

#include "kerberos/proto_bindings/kerberos_service.pb.h"

namespace kerberos {

std::ostream& operator<<(std::ostream& os, ConfigErrorCode code) {
  switch (code) {
    case CONFIG_ERROR_NONE:
      return os << "OK";
    case CONFIG_ERROR_SECTION_NESTED_IN_GROUP:
      return os << "Section nested in group";
    case CONFIG_ERROR_SECTION_SYNTAX:
      return os << "ESection syntax error, expected '[section]'";
    case CONFIG_ERROR_EXPECTED_OPENING_CURLY_BRACE:
      return os << "Expected opening curly brace '{'";
    case CONFIG_ERROR_EXTRA_CURLY_BRACE:
      return os << "Extra curly brace";
    case CONFIG_ERROR_RELATION_SYNTAX:
      return os << "Relation syntax error, expected 'key = ...'";
    case CONFIG_ERROR_KEY_NOT_SUPPORTED:
      return os << "Key not supported";
    case CONFIG_ERROR_SECTION_NOT_SUPPORTED:
      return os << "Section not supported";
    case CONFIG_ERROR_KRB5_FAILED_TO_PARSE:
      return os << "KRB5 failed to parse";
    case CONFIG_ERROR_COUNT:
      NOTREACHED();
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const ConfigErrorInfo& error_info) {
  if (error_info.code() == CONFIG_ERROR_NONE)
    return os << "[no error]";

  return os << error_info.code() << " at line " << error_info.line_index();
}

class ConfigValidatorTest : public ::testing::Test {
 protected:
  void ExpectNoError(const char* krb5conf) {
    ConfigErrorInfo error_info = config_validator_.Validate(krb5conf);

    EXPECT_TRUE(error_info.has_code()) << error_info;
    EXPECT_EQ(CONFIG_ERROR_NONE, error_info.code()) << error_info;
    EXPECT_FALSE(error_info.has_line_index()) << error_info;
  }

  void ExpectError(const std::string& krb5conf,
                   ConfigErrorCode code,
                   int line_index) {
    ConfigErrorInfo error_info = config_validator_.Validate(krb5conf);

    EXPECT_TRUE(error_info.has_code()) << error_info;
    EXPECT_EQ(code, error_info.code()) << error_info;
    EXPECT_TRUE(error_info.has_line_index()) << error_info;
    EXPECT_EQ(line_index, error_info.line_index()) << error_info;
  }

  ConfigValidator config_validator_;
};

TEST_F(ConfigValidatorTest, ValidConfig) {
  constexpr char kKrb5Conf[] = R"(
# Comment
; Another comment

[libdefaults]
  clockskew = 123
  permitted_enctypes = only the good ones
  renew_lifetime* = 7d
  ticket_lifetime* = 1d
  A.EXAMPLE.COM = {
    clockskew = 300
  }
  B.EXAMPLE.COM =
  {
    ; Tests = whether { can be on new line
    clockskew = 500
  }

[realms]
  kdc = 5.6.7.8
  EXAMPLE.COM = {
    kdc = 1.2.3.4
    admin_server = kdc.example.com
    auth_to_local = RULE:[2:$1](johndoe)s/^.*$/guest/
    auth_to_local_names = {
      hans = jack
      joerg = jerk
    }
  }*

[domain_realm]*
  any.thing = IS.ACCEPTED.HERE

[capaths]
    here = AS.WELL)";

  ExpectNoError(kKrb5Conf);
}

TEST_F(ConfigValidatorTest, Empty) {
  ExpectNoError("");
  ExpectNoError("\n");
  ExpectNoError("\n\n\n");
  ExpectNoError("[libdefaults]");
  ExpectNoError("[libdefaults]\n");
  ExpectNoError("[libdefaults]\n\n\n");
}

TEST_F(ConfigValidatorTest, ModulesAndIncludesBlocked) {
  ExpectError("module MODULEPATH:RESIDUAL", CONFIG_ERROR_KEY_NOT_SUPPORTED, 0);
  ExpectError("include /path/to/file", CONFIG_ERROR_KEY_NOT_SUPPORTED, 0);
  ExpectError("includedir /path/to/files", CONFIG_ERROR_KEY_NOT_SUPPORTED, 0);

  constexpr char kKrb5Conf[] = R"(
[libdefaults]
  includedir /path/to/files)";
  ExpectError(kKrb5Conf, CONFIG_ERROR_KEY_NOT_SUPPORTED, 2);
}

TEST_F(ConfigValidatorTest, UnsupportedLibdefaultsKey) {
  constexpr char kKrb5Conf[] = R"(
[libdefaults]
  stonkskew = 123)";
  ExpectError(kKrb5Conf, CONFIG_ERROR_KEY_NOT_SUPPORTED, 2);
}

TEST_F(ConfigValidatorTest, UnsupportedNestedLibdefaultsKey) {
  constexpr char kKrb5Conf[] = R"(
[libdefaults]
  A.EXAMPLE.COM = {
    stonkskew = 300
  })";
  ExpectError(kKrb5Conf, CONFIG_ERROR_KEY_NOT_SUPPORTED, 3);
}

TEST_F(ConfigValidatorTest, UnsupportedRealmKey) {
  constexpr char kKrb5Conf[] = R"(
[realms]
  BEISPIEL.FIR = {
    meister_svz = svz.beispiel.fir
  })";
  ExpectError(kKrb5Conf, CONFIG_ERROR_KEY_NOT_SUPPORTED, 3);
}

TEST_F(ConfigValidatorTest, RelationSyntaxErrorKeyWithoutEquals) {
  constexpr char kKrb5Conf[] = R"(
[libdefaults]
  kdc: kdc.example.com
)";
  ExpectError(kKrb5Conf, CONFIG_ERROR_RELATION_SYNTAX, 2);
}

TEST_F(ConfigValidatorTest, UnsupportedSection) {
  ExpectError("[appdefaults]", CONFIG_ERROR_SECTION_NOT_SUPPORTED, 0);
}

TEST_F(ConfigValidatorTest, SectionNestedInGroup) {
  constexpr char kKrb5Conf[] = R"(
[realms]
  EXAMPLE.COM = {
    [libdefaults]
  })";
  ExpectError(kKrb5Conf, CONFIG_ERROR_SECTION_NESTED_IN_GROUP, 3);
}

TEST_F(ConfigValidatorTest, MissingSectionBrackets) {
  ExpectError("[realms", CONFIG_ERROR_SECTION_SYNTAX, 0);
}

TEST_F(ConfigValidatorTest, SpacesBeforeSectionEndMarker) {
  // Note that the krb5 parser appears to accept spaces before the ']', but
  // it's a different section than without the spaces, so we reject it.
  ExpectError("[realms  ]", CONFIG_ERROR_SECTION_NOT_SUPPORTED, 0);
}

TEST_F(ConfigValidatorTest, ExtraStuffBeforeSectionBrackets) {
  ExpectError("extra [realms]", CONFIG_ERROR_RELATION_SYNTAX, 0);
}

TEST_F(ConfigValidatorTest, ExtraStuffAfterSectionBrackets) {
  ExpectError("[realms] extra", CONFIG_ERROR_SECTION_SYNTAX, 0);
}

TEST_F(ConfigValidatorTest, FinalMarkersAllowed) {
  ExpectNoError("[libdefaults]* \nclockskew*=9");
}

TEST_F(ConfigValidatorTest, FinalMarkersWithSpacesNotAllowed) {
  ExpectError("[libdefaults] *)", CONFIG_ERROR_SECTION_SYNTAX, 0);
  ExpectError("[libdefaults]\nclockskew *=9", CONFIG_ERROR_RELATION_SYNTAX, 1);
}

TEST_F(ConfigValidatorTest, RelationSyntaxError) {
  ExpectError("[libdefaults]\nclockskew", CONFIG_ERROR_RELATION_SYNTAX, 1);
  ExpectError("[libdefaults]\nclockskew ", CONFIG_ERROR_RELATION_SYNTAX, 1);
  ExpectError("[libdefaults]\nclockskew* ", CONFIG_ERROR_RELATION_SYNTAX, 1);
  ExpectError("[libdefaults]\n=clockskew*", CONFIG_ERROR_RELATION_SYNTAX, 1);
}

TEST_F(ConfigValidatorTest, TwoEqualSignsAllowed) {
  ExpectNoError("[libdefaults]\nclockskew=1=2");
}

TEST_F(ConfigValidatorTest, RelationSyntaxEdgeCases) {
  ExpectError("*", CONFIG_ERROR_RELATION_SYNTAX, 0);
  ExpectError("*=", CONFIG_ERROR_RELATION_SYNTAX, 0);
  ExpectError("=", CONFIG_ERROR_RELATION_SYNTAX, 0);

  ExpectError(" *", CONFIG_ERROR_RELATION_SYNTAX, 0);
  ExpectError(" *=", CONFIG_ERROR_RELATION_SYNTAX, 0);
  ExpectError(" =", CONFIG_ERROR_RELATION_SYNTAX, 0);

  ExpectError("* ", CONFIG_ERROR_RELATION_SYNTAX, 0);
  ExpectError("*= ", CONFIG_ERROR_RELATION_SYNTAX, 0);
  ExpectError("= ", CONFIG_ERROR_RELATION_SYNTAX, 0);

  ExpectError(" * ", CONFIG_ERROR_RELATION_SYNTAX, 0);
  ExpectError(" *= ", CONFIG_ERROR_RELATION_SYNTAX, 0);
  ExpectError(" = ", CONFIG_ERROR_RELATION_SYNTAX, 0);

  ExpectError(" * = ", CONFIG_ERROR_RELATION_SYNTAX, 0);
}

TEST_F(ConfigValidatorTest, WhitespaceBeforeAndAfterSectionBrackets) {
  ExpectNoError("   [realms]   ");
}

TEST_F(ConfigValidatorTest, MissingOpeningCurlyBrace) {
  constexpr char kKrb5Conf[] = R"(
[realms]
  EXAMPLE.COM =

    kdc = kdc.example.com
  })";
  ExpectError(kKrb5Conf, CONFIG_ERROR_EXPECTED_OPENING_CURLY_BRACE, 3);
}

TEST_F(ConfigValidatorTest, ExtraCurlyBraceFound) {
  constexpr char kKrb5Conf[] = R"(
  [realms]
  EXAMPLE.COM =
  {
    kdc = kdc.example.com
  }
})";
  ExpectError(kKrb5Conf, CONFIG_ERROR_EXTRA_CURLY_BRACE, 6);
}

// Things that the fuzzer found.
TEST_F(ConfigValidatorTest, FuzzerRegressionTests) {
  // Code was looking at character after "include" to check if it's a space.
  ExpectError("include", CONFIG_ERROR_KEY_NOT_SUPPORTED, 0);

  // Code was accepting "[realms\0]" as a valid section. Embedded \0's should be
  // handled in a c_str() kind of way.
  std::string krb5confWithZero = "[realms\0]";
  krb5confWithZero[7] = 0;
  ExpectError(krb5confWithZero, CONFIG_ERROR_SECTION_SYNTAX, 0);

  // Code was allowing spaces in keys. Note that ConfigValidator allows all keys
  // in the [domain_realm] section, but it should still check spaces!
  ExpectError("[domain_realm]\nkey x=", CONFIG_ERROR_RELATION_SYNTAX, 1);

  // \r should not be counted as newline character.
  ExpectError("[domain_realm]\rkey=", CONFIG_ERROR_SECTION_SYNTAX, 0);

  // Double == is always a relation, cannot be the start of a group.
  ExpectError("[capaths]\nkey==\n{", CONFIG_ERROR_RELATION_SYNTAX, 2);
}

}  // namespace kerberos
