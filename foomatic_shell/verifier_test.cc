// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "foomatic_shell/verifier.h"

#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "foomatic_shell/parser.h"
#include "foomatic_shell/scanner.h"

namespace foomatic_shell {

bool VerifyScript(const std::string& command) {
  Script script;
  std::vector<Token> tokens;

  Scanner scanner(command);
  if (!scanner.ParseWholeInput(&tokens)) {
    return false;
  }
  Parser parser(std::move(tokens));
  if (!parser.ParseWholeInput(&script)) {
    return false;
  }

  Verifier verifier;
  return verifier.VerifyScript(&script);
}

TEST(Verifier, cat) {
  EXPECT_TRUE(VerifyScript("cat"));
}

TEST(Verifier, cat2) {
  EXPECT_TRUE(VerifyScript("cat -"));
}

TEST(Verifier, catFail) {
  EXPECT_FALSE(VerifyScript("cat somefile"));
}

TEST(Verifier, cut) {
  EXPECT_TRUE(VerifyScript("cut -b 1-"));
}

TEST(Verifier, cutAllowedShortParameters) {
  EXPECT_TRUE(VerifyScript("cut -b 12 -c 34 -d 56 -f 78"));
  EXPECT_FALSE(VerifyScript("cut -b 12 12"));
  EXPECT_TRUE(VerifyScript("cut -b123"));
  EXPECT_FALSE(VerifyScript("cut -b123 x"));
  EXPECT_TRUE(VerifyScript("cut -b123 -"));
  EXPECT_FALSE(VerifyScript("cut -b"));
  EXPECT_TRUE(VerifyScript("cut -nsz"));
  EXPECT_FALSE(VerifyScript("cut -nsz x"));
  EXPECT_FALSE(VerifyScript("cut -nszx"));
}

TEST(Verifier, cutAllowedLongParameters) {
  EXPECT_TRUE(VerifyScript("cut --bytes=123 --characters xxx --delimiter=x"));
  EXPECT_TRUE(VerifyScript("cut --byt=123 --ch=xxx --delim xxx --fields=1"));
  EXPECT_TRUE(VerifyScript("cut --fiel=1 --output-delimiter=zzz"));
  EXPECT_FALSE(VerifyScript("cut --byts=123"));
  EXPECT_FALSE(VerifyScript("cut --bytes"));
  EXPECT_TRUE(VerifyScript("cut --complement"));
  EXPECT_TRUE(VerifyScript("cut --only-delimited"));
  EXPECT_TRUE(VerifyScript("cut --zero-terminated"));
  EXPECT_TRUE(VerifyScript("cut --compl --only-de --ze"));
  EXPECT_FALSE(VerifyScript("cut --compl=x"));
  EXPECT_FALSE(VerifyScript("cut --ze 123"));
}

TEST(Verifier, cutNonOptionParameters) {
  // Only '-' is allowed.
  EXPECT_TRUE(VerifyScript("cut -"));
  EXPECT_FALSE(VerifyScript("cut x"));
  EXPECT_TRUE(VerifyScript("cut --b=123 -- -"));
  EXPECT_FALSE(VerifyScript("cut -- --b=123 -"));
}

TEST(Verifier, cutEmptyParameter) {
  EXPECT_TRUE(VerifyScript("cut -b 12"));
  EXPECT_TRUE(VerifyScript("cut -b ''"));
  EXPECT_TRUE(VerifyScript("cut -b \"\""));
  EXPECT_FALSE(VerifyScript("cut -b '' x"));
  EXPECT_TRUE(VerifyScript("cut --bytes=123"));
  EXPECT_TRUE(VerifyScript("cut --bytes="));
  EXPECT_TRUE(VerifyScript("cut --bytes ''"));
  EXPECT_FALSE(VerifyScript("cut --bytes '' x"));
}

TEST(Verifier, date) {
  EXPECT_TRUE(VerifyScript("date"));
}

TEST(Verifier, dateFail) {
  EXPECT_FALSE(VerifyScript("date -s"));
}

TEST(Verifier, dateFail2) {
  EXPECT_FALSE(VerifyScript("date --set"));
}

TEST(Verifier, echo) {
  EXPECT_TRUE(VerifyScript("echo something"));
}

TEST(Verifier, echoFail) {
  using std::string_literals::operator""s;
  EXPECT_FALSE(VerifyScript("echo some\0thing"s));
}

TEST(Verifier, echoWithVariable) {
  EXPECT_TRUE(VerifyScript("NOPDF=abc echo something"));
}

TEST(Verifier, echoWithVariableFail) {
  using std::string_literals::operator""s;
  EXPECT_FALSE(VerifyScript("NOPDF=ab\0c echo something"s));
}

TEST(Verifier, gs) {
  EXPECT_TRUE(VerifyScript("gs -dSAFER -sOutputFile=- somefile.ps"));
}

TEST(Verifier, gsFail) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER -sOutputFile=- -dNOSAFER somefile.ps"));
}

TEST(Verifier, gsFail1) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER -sOutputFile=- -dDELAYSAFER file.ps"));
}

TEST(Verifier, gsFail2) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER -sOutputFile=- -dALLOWPSTRANSPARENCY"));
}

TEST(Verifier, gsFail3) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER -sOutputFile=xyz.out somefile.ps"));
}

TEST(Verifier, gsFail4) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER somefile.ps"));
}

TEST(Verifier, gsFail5) {
  EXPECT_FALSE(VerifyScript("gs -sOutputFile=- somefile.ps"));
}

TEST(Verifier, gsFail6) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER -sOutputFile=- -dNOSAFER=true"));
}

TEST(Verifier, gsFail7) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER -sOutputFile=- -o somefile"));
}

TEST(Verifier, gsFail8) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER -sOutputFile=- -c"));
}

TEST(Verifier, gsFail9) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER -sOutputFile=- --permit-file-write=x"));
}

TEST(Verifier, gsFail10) {
  EXPECT_FALSE(VerifyScript("gs -dSAFER -sOutputFile=- -Ixxx"));
}

TEST(Verifier, gsOK) {
  EXPECT_TRUE(VerifyScript("gs -dPARANOIDSAFER -sOutputFile=%stdout"));
}

TEST(Verifier, gsUppFile) {
  EXPECT_TRUE(VerifyScript("gs -dPARANOIDSAFER -sOutputFile=%stdout @aaa.upp"));
  EXPECT_FALSE(VerifyScript("gs -dPARANOIDSAFER -sOutputFile=%stdout @aaa.up"));
  EXPECT_FALSE(VerifyScript("gs -dPARANOIDSAFER -sOutputFile=%stdout @/a.upp"));
}

TEST(Verifier, pdftops) {
  EXPECT_TRUE(VerifyScript("pdftops"));
}

TEST(Verifier, printf) {
  EXPECT_TRUE(VerifyScript("printf"));
}

TEST(Verifier, sed) {
  EXPECT_TRUE(VerifyScript("sed 's/foo/bar/' somefile"));
}

TEST(Verifier, sedFail) {
  EXPECT_FALSE(VerifyScript("sed -ui 's/foo/bar/' somefile"));
}

TEST(Verifier, sedFail2) {
  EXPECT_FALSE(VerifyScript("sed --in-place 's/foo/bar/' somefile"));
}

TEST(Verifier, disallowedCommand) {
  EXPECT_FALSE(VerifyScript("rm"));
}

}  // namespace foomatic_shell
