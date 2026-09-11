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
  EXPECT_TRUE(VerifyScript("date +format"));
  EXPECT_FALSE(VerifyScript("date xxx"));
  EXPECT_TRUE(VerifyScript("date --date='@2147483647' +format"));
}

TEST(Verifier, dateLongParameters) {
  EXPECT_TRUE(VerifyScript("date --debug --date my/date"));
  EXPECT_TRUE(VerifyScript("date --iso-8601"));
  EXPECT_TRUE(VerifyScript("date --is=xxx"));
  EXPECT_TRUE(VerifyScript("date --rfc-email"));
  EXPECT_TRUE(VerifyScript("date --rfc-3339=FMT"));
  EXPECT_TRUE(VerifyScript("date --rfc-3 FMT"));
  EXPECT_TRUE(VerifyScript("date --utc --universal"));
  EXPECT_FALSE(VerifyScript("date --file=DATEFILE"));
  EXPECT_FALSE(VerifyScript("date --file"));
  EXPECT_FALSE(VerifyScript("date --reference=FILE"));
  EXPECT_FALSE(VerifyScript("date --reference"));
  EXPECT_FALSE(VerifyScript("date --set=STRING"));
  EXPECT_FALSE(VerifyScript("date --set"));
  EXPECT_FALSE(VerifyScript("date --help"));
  EXPECT_FALSE(VerifyScript("date --version"));
}

TEST(Verifier, dateShortParameters) {
  EXPECT_TRUE(VerifyScript("date -d 'TZ=\"America/Los_Angeles\" 09:00'"));
  EXPECT_FALSE(VerifyScript("date -d"));
  EXPECT_TRUE(VerifyScript("date -I"));
  EXPECT_TRUE(VerifyScript("date -Ixxx"));
  EXPECT_TRUE(VerifyScript("date -R"));
  EXPECT_TRUE(VerifyScript("date -u"));
  EXPECT_FALSE(VerifyScript("date -f DATEFILE"));
  EXPECT_FALSE(VerifyScript("date -f"));
  EXPECT_FALSE(VerifyScript("date -r FILE"));
  EXPECT_FALSE(VerifyScript("date -r"));
  EXPECT_FALSE(VerifyScript("date -s STRING"));
  EXPECT_FALSE(VerifyScript("date -s"));
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
  EXPECT_TRUE(VerifyScript("sed"));
  EXPECT_TRUE(VerifyScript("sed -"));
  EXPECT_TRUE(VerifyScript("sed 's/foo/bar/' -"));
}

TEST(Verifier, sedAddSandbox) {
  const std::string input = "sed 's/foo/bar/' -";
  Scanner scanner(input);
  std::vector<Token> tokens;
  ASSERT_TRUE(scanner.ParseWholeInput(&tokens));
  Parser parser(std::move(tokens));
  Script script;
  ASSERT_TRUE(parser.ParseWholeInput(&script));
  Verifier verifier;
  EXPECT_TRUE(verifier.VerifyScript(&script));
  ASSERT_EQ(script.pipelines.size(), 1);
  ASSERT_EQ(script.pipelines[0].segments.size(), 1);
  Command* cmd = script.pipelines[0].segments[0].command.get();
  ASSERT_NE(cmd, nullptr);
  ASSERT_EQ(cmd->parameters.size(), 3);
  EXPECT_EQ(cmd->parameters[0].value, "--sandbox");
}

TEST(Verifier, sedFail) {
  EXPECT_FALSE(VerifyScript("sed -ui 's/foo/bar/' -"));
}

TEST(Verifier, sedFail2) {
  EXPECT_FALSE(VerifyScript("sed --in-place 's/foo/bar/' -"));
}

TEST(Verifier, sedAllowedInputFile) {
  EXPECT_TRUE(VerifyScript("sed -- /my/expresion -"));
  EXPECT_TRUE(VerifyScript("sed -e /my/expresion -- -"));
}

TEST(Verifier, sedFailForbiddenInputFile) {
  EXPECT_FALSE(VerifyScript("sed -- /my/expresion /path/file"));
  EXPECT_FALSE(VerifyScript("sed -e /my/expresion -- /path/file"));
  EXPECT_FALSE(VerifyScript("sed --expression=/my/expresion -- /path/file"));
}

TEST(Verifier, sedForbiddenParameter) {
  EXPECT_FALSE(VerifyScript("sed -f's/foo/bar/' -"));
  EXPECT_FALSE(VerifyScript("sed --file='s/foo/bar/' -"));
}

TEST(Verifier, sedExpression) {
  // First non-option parameter is an expression, if no -e, --expression
  // parameters were given. All other non-option parameters different than "-"
  // are forbidden.
  EXPECT_TRUE(VerifyScript("sed /my/expresion"));
  EXPECT_FALSE(VerifyScript("sed exp /my/expresion"));
  EXPECT_TRUE(VerifyScript("sed -e /my/expresion"));
  EXPECT_TRUE(VerifyScript("sed -eexp"));
  EXPECT_FALSE(VerifyScript("sed -e /my/expresion exp"));
  EXPECT_FALSE(VerifyScript("sed --e /my/expresion exp"));
  EXPECT_FALSE(VerifyScript("sed exp -eexp"));
  EXPECT_FALSE(VerifyScript("sed exp --e=exp"));
  EXPECT_TRUE(VerifyScript("sed --expression=/my/expresion"));
  EXPECT_TRUE(VerifyScript("sed --expression /my/expresion"));
  EXPECT_FALSE(VerifyScript("sed /my/expresion --expression exp"));
  EXPECT_FALSE(VerifyScript("sed --expression= /my/expresion"));
  EXPECT_FALSE(VerifyScript("sed exp --expression=/my/expresion"));
  // Parameters -e, --expression must be followed by an expression.
  EXPECT_FALSE(VerifyScript("sed --expression"));
  EXPECT_FALSE(VerifyScript("sed --e"));
  EXPECT_FALSE(VerifyScript("sed -e"));
}

TEST(Verifier, disallowedCommand) {
  EXPECT_FALSE(VerifyScript("rm"));
}

}  // namespace foomatic_shell
