// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipp_in_json.h"

#include <cstdint>
#include <string>
#include <vector>

#include <base/strings/string_util.h>
#include <chromeos/libipp/attribute.h>
#include <chromeos/libipp/frame.h>
#include <chromeos/libipp/parser.h>
#include <gtest/gtest.h>

namespace {

TEST(IppToJson, StringAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::client_error_gone, ipp::Version::_2_0, 1,
                   false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::document_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(
      grp->AddAttr("test-attr", ipp::ValueTag::textWithoutLanguage, "value"),
      ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json,
            R"({"response":{"document-attributes":{)"
            R"("test-attr":{"type":"textWithoutLanguage","value":"value"})"
            R"(}},"status":"client-error-gone"})");
}

TEST(IppToSimpleJson, StringAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::client_error_gone, ipp::Version::_2_0, 1,
                   false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(
      grp->AddAttr("test-attr", ipp::ValueTag::textWithoutLanguage, "value"),
      ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToSimpleJson(frame, log, "", &json));
  base::RemoveChars(json, base::kWhitespaceASCII, &json);
  EXPECT_EQ(json, R"({"printer-attributes":{)"
                  R"("test-attr":"value"})"
                  R"(,"status":"client-error-gone"})");
}

TEST(IppToJson, StringWithLanguageAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::client_error_gone, ipp::Version::_2_0, 1,
                   false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::document_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("test-attr", ipp::ValueTag::textWithLanguage,
                         ipp::StringWithLanguage("Value", "Language")),
            ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json, R"({"response":{"document-attributes":{)"
                  R"("test-attr":{"type":"textWithLanguage",)"
                  R"("value":{"language":"Language","value":"Value"})"
                  R"(}}},"status":"client-error-gone"})");
}

TEST(IppToSimpleJson, StringWithLanguageAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::client_error_gone, ipp::Version::_2_0, 1,
                   false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("test-attr", ipp::ValueTag::textWithLanguage,
                         ipp::StringWithLanguage("Value", "Language")),
            ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToSimpleJson(frame, log, "", &json));
  base::RemoveChars(json, base::kWhitespaceASCII, &json);
  EXPECT_EQ(json, R"({"printer-attributes":{)"
                  R"("test-attr":"Value"})"
                  R"(,"status":"client-error-gone"})");
}

TEST(IppToJson, IntegerAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, grp), ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("abc", ipp::ValueTag::integer, 123), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("abc":{"type":"integer","value":123})"
                  R"(}},"status":"successful-ok"})");
}

TEST(IppToSimpleJson, IntegerAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("abc", ipp::ValueTag::integer, 123), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToSimpleJson(frame, log, "", &json));
  base::RemoveChars(json, base::kWhitespaceASCII, &json);
  EXPECT_EQ(json, R"({"printer-attributes":{)"
                  R"("abc":123})"
                  R"(,"status":"successful-ok"})");
}

TEST(IppToJson, EnumAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, grp), ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("abcd", ipp::ValueTag::enum_, 1234), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("abcd":{"type":"enum","value":1234})"
                  R"(}},"status":"successful-ok"})");
}

TEST(IppToSimpleJson, EnumAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("abcd", ipp::ValueTag::enum_, 1234), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToSimpleJson(frame, log, "", &json));
  base::RemoveChars(json, base::kWhitespaceASCII, &json);
  EXPECT_EQ(json, R"({"printer-attributes":{)"
                  R"("abcd":1234})"
                  R"(,"status":"successful-ok"})");
}

TEST(IppToJson, BooleanAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, grp), ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("attr1", true), ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("attr2", false), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("attr1":{"type":"boolean","value":true},)"
                  R"("attr2":{"type":"boolean","value":false})"
                  R"(}},"status":"successful-ok"})");
}

TEST(IppToSimpleJson, BooleanAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("attr1", true), ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("attr2", false), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToSimpleJson(frame, log, "", &json));
  base::RemoveChars(json, base::kWhitespaceASCII, &json);
  EXPECT_EQ(json, R"({"printer-attributes":{)"
                  R"("attr1":true,)"
                  R"("attr2":false})"
                  R"(,"status":"successful-ok"})");
}

TEST(IppToJson, OutOfBandAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("attr", ipp::ValueTag::not_settable), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json, R"({"response":{"printer-attributes":{)"
                  R"("attr":"not-settable")"
                  R"(}},"status":"successful-ok"})");
}

TEST(IppToSimpleJson, OutOfBandAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("attr", ipp::ValueTag::not_settable), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToSimpleJson(frame, log, "", &json));
  base::RemoveChars(json, base::kWhitespaceASCII, &json);
  EXPECT_EQ(json, R"({"printer-attributes":{)"
                  R"("attr":"not-settable"})"
                  R"(,"status":"successful-ok"})");
}

TEST(IppToJson, SetOfIntegersAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, grp), ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("attr", std::vector<int32_t>{1, 2, 3}),
            ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("attr":{"type":"integer","value":[1,2,3]})"
                  R"(}},"status":"successful-ok"})");
}

TEST(IppToSimpleJson, SetOfIntegersAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("attr", std::vector<int32_t>{1, 2, 3}),
            ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToSimpleJson(frame, log, "", &json));
  base::RemoveChars(json, base::kWhitespaceASCII, &json);
  EXPECT_EQ(json, R"({"printer-attributes":{)"
                  R"("attr":[1,2,3]})"
                  R"(,"status":"successful-ok"})");
}

TEST(IppToJson, CollectionAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, grp), ipp::Code::kOK);
  ipp::CollsView::iterator coll;
  ASSERT_EQ(grp->AddAttr("attr", coll), ipp::Code::kOK);
  EXPECT_EQ(coll->AddAttr("attr", true), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("attr":{"type":"collection","value":)"
                  R"({"attr":{"type":"boolean","value":true}})"
                  R"(}}},"status":"successful-ok"})");
}

TEST(IppToSimpleJson, CollectionAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  ipp::CollsView::iterator coll;
  ASSERT_EQ(grp->AddAttr("attr", coll), ipp::Code::kOK);
  EXPECT_EQ(coll->AddAttr("attr", true), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToSimpleJson(frame, log, "", &json));
  base::RemoveChars(json, base::kWhitespaceASCII, &json);
  EXPECT_EQ(json, R"({"printer-attributes":{)"
                  R"("attr":{"attr":true}})"
                  R"(,"status":"successful-ok"})");
}

TEST(IppToJson, FilteredAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, grp), ipp::Code::kOK);
  ipp::CollsView::iterator col1;
  ASSERT_EQ(grp->AddAttr("col1-in", col1), ipp::Code::kOK);
  EXPECT_EQ(col1->AddAttr("attr", true), ipp::Code::kOK);
  ipp::CollsView::iterator col2;
  ASSERT_EQ(grp->AddAttr("col2-out", col2), ipp::Code::kOK);
  EXPECT_EQ(col2->AddAttr("attr", true), ipp::Code::kOK);
  ipp::CollsView::iterator col3;
  ASSERT_EQ(grp->AddAttr("col3-in", col3), ipp::Code::kOK);
  EXPECT_EQ(col3->AddAttr("attr", true), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "-in", true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("col1-in":{"type":"collection","value":)"
                  R"({"attr":{"type":"boolean","value":true}}},)"
                  R"("col3-in":{"type":"collection","value":)"
                  R"({"attr":{"type":"boolean","value":true}})"
                  R"(}}},"status":"successful-ok"})");
}

TEST(IppToSimpleJson, FilteredAttribute) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::printer_attributes, grp),
            ipp::Code::kOK);
  ipp::CollsView::iterator col1;
  ASSERT_EQ(grp->AddAttr("col1-in", col1), ipp::Code::kOK);
  EXPECT_EQ(col1->AddAttr("attr", true), ipp::Code::kOK);
  ipp::CollsView::iterator col2;
  ASSERT_EQ(grp->AddAttr("col2-out", col2), ipp::Code::kOK);
  EXPECT_EQ(col2->AddAttr("attr", true), ipp::Code::kOK);
  ipp::CollsView::iterator col3;
  ASSERT_EQ(grp->AddAttr("col3-in", col3), ipp::Code::kOK);
  EXPECT_EQ(col3->AddAttr("attr", true), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToSimpleJson(frame, log, "-in", &json));
  base::RemoveChars(json, base::kWhitespaceASCII, &json);
  EXPECT_EQ(json, R"({"printer-attributes":{)"
                  R"("col1-in":)"
                  R"({"attr":true},)"
                  R"("col3-in":)"
                  R"({"attr":true})"
                  R"(},"status":"successful-ok"})");
}

TEST(IppToJson, TwoEmptyGroups) {
  ipp::CollsView::iterator grp;
  ipp::SimpleParserLog log;

  ipp::Frame frame(ipp::Status::successful_ok, ipp::Version::_1_1, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, grp), ipp::Code::kOK);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, grp), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":[{},{}]},)"
                  R"("status":"successful-ok"})");
}

TEST(IppToJson, ParsingLogs) {
  ipp::SimpleParserLog log;
  log.AddParserError(
      ipp::ParserError{ipp::AttrPath(ipp::GroupTag::job_attributes),
                       ipp::ParserCode::kValueInvalidSize});

  ipp::Frame frame(ipp::Status::client_error_gone, ipp::Version::_2_0, 1,
                   false);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, "", true, &json));
  EXPECT_EQ(json, R"({"parsing_logs":["job-attributes; ValueInvalidSize"],)"
                  R"("response":{},"status":"client-error-gone"})");
}

}  // namespace
