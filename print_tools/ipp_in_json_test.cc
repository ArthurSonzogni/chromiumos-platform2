// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipp_in_json.h"

#include <cstdint>
#include <string>
#include <vector>

#include <chromeos/libipp/attribute.h>
#include <chromeos/libipp/frame.h>
#include <gtest/gtest.h>

namespace {

TEST(IppToJson, StringAttribute) {
  ipp::Collection* grp = nullptr;
  ipp::ParsingResults log{.whole_buffer_was_parsed = true};

  ipp::Frame frame(ipp::Version::_2_0, ipp::Status::client_error_gone, 1,
                   false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::document_attributes, &grp),
            ipp::Code::kOK);
  ASSERT_NE(grp, nullptr);
  EXPECT_EQ(grp->AddAttr("test-attr", ipp::ValueTag::mimeMediaType, "value"),
            ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, true, &json));
  EXPECT_EQ(json, R"({"response":{"document-attributes":{)"
                  R"("test-attr":{"type":"mimeMediaType","value":"value"})"
                  R"(}},"status":"client-error-gone"})");
}

TEST(IppToJson, IntegerAttribute) {
  ipp::Collection* grp = nullptr;
  ipp::ParsingResults log{.whole_buffer_was_parsed = true};

  ipp::Frame frame(ipp::Version::_1_1, ipp::Status::successful_ok, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, &grp),
            ipp::Code::kOK);
  ASSERT_NE(grp, nullptr);
  EXPECT_EQ(grp->AddAttr("abc", ipp::ValueTag::integer, 123), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("abc":{"type":"integer","value":123})"
                  R"(}},"status":"successful-ok"})");
}

TEST(IppToJson, BooleanAttribute) {
  ipp::Collection* grp = nullptr;
  ipp::ParsingResults log{.whole_buffer_was_parsed = true};

  ipp::Frame frame(ipp::Version::_1_1, ipp::Status::successful_ok, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, &grp),
            ipp::Code::kOK);
  ASSERT_NE(grp, nullptr);
  EXPECT_EQ(grp->AddAttr("attr1", true), ipp::Code::kOK);
  EXPECT_EQ(grp->AddAttr("attr2", false), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("attr1":{"type":"boolean","value":true},)"
                  R"("attr2":{"type":"boolean","value":false})"
                  R"(}},"status":"successful-ok"})");
}

TEST(IppToJson, SetOfIntegersAttribute) {
  ipp::Collection* grp = nullptr;
  ipp::ParsingResults log{.whole_buffer_was_parsed = true};

  ipp::Frame frame(ipp::Version::_1_1, ipp::Status::successful_ok, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, &grp),
            ipp::Code::kOK);
  ASSERT_NE(grp, nullptr);
  EXPECT_EQ(grp->AddAttr("attr", std::vector<int32_t>{1, 2, 3}),
            ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("attr":{"type":"integer","value":[1,2,3]})"
                  R"(}},"status":"successful-ok"})");
}

TEST(IppToJson, CollectionAttribute) {
  ipp::Collection* grp = nullptr;
  ipp::ParsingResults log{.whole_buffer_was_parsed = true};

  ipp::Frame frame(ipp::Version::_1_1, ipp::Status::successful_ok, 1, false);
  ASSERT_EQ(frame.AddGroup(ipp::GroupTag::job_attributes, &grp),
            ipp::Code::kOK);
  ASSERT_NE(grp, nullptr);
  ipp::Collection* coll;
  ASSERT_EQ(grp->AddAttr("attr", coll), ipp::Code::kOK);
  EXPECT_EQ(coll->AddAttr("attr", true), ipp::Code::kOK);

  std::string json;
  EXPECT_TRUE(ConvertToJson(frame, log, true, &json));
  EXPECT_EQ(json, R"({"response":{"job-attributes":{)"
                  R"("attr":{"type":"collection","value":)"
                  R"({"attr":{"type":"boolean","value":true}})"
                  R"(}}},"status":"successful-ok"})");
}

}  // namespace
