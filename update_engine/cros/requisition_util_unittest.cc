//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/requisition_util.h"

#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_string_value_serializer.h>
#include <gtest/gtest.h>

#include "update_engine/common/test_utils.h"

using chromeos_update_engine::test_utils::WriteFileString;
using std::string;

namespace {

const char kRemoraJSON[] =
    "{\n"
    "   \"the_list\": [ \"val1\", \"val2\" ],\n"
    "   \"enrollment\": {\n"
    "      \"autostart\": true,\n"
    "      \"can_exit\": false,\n"
    "      \"device_requisition\": \"remora\"\n"
    "   },\n"
    "   \"some_String\": \"1337\",\n"
    "   \"some_int\": 42\n"
    "}\n";

const char kNoEnrollmentJSON[] =
    "{\n"
    "   \"the_list\": [ \"val1\", \"val2\" ],\n"
    "   \"enrollment\": {\n"
    "      \"autostart\": true,\n"
    "      \"can_exit\": false,\n"
    "      \"device_requisition\": \"\"\n"
    "   },\n"
    "   \"some_String\": \"1337\",\n"
    "   \"some_int\": 42\n"
    "}\n";
}  // namespace

namespace chromeos_update_engine {

class RequisitionUtilTest : public ::testing::Test {
 protected:
  std::unique_ptr<base::Value> JsonToUniquePtrValue(const string& json_string) {
    int error_code;
    std::string error_msg;

    JSONStringValueDeserializer deserializer(json_string);

    return deserializer.Deserialize(&error_code, &error_msg);
  }
};

TEST_F(RequisitionUtilTest, BadJsonReturnsEmpty) {
  std::unique_ptr<base::Value> root = JsonToUniquePtrValue("this isn't JSON");
  EXPECT_EQ("", ReadDeviceRequisition(root.get()));
}

TEST_F(RequisitionUtilTest, NoFileReturnsEmpty) {
  std::unique_ptr<base::Value> root = nullptr;
  EXPECT_EQ("", ReadDeviceRequisition(root.get()));
}

TEST_F(RequisitionUtilTest, EnrollmentRequisition) {
  std::unique_ptr<base::Value> root = JsonToUniquePtrValue(kRemoraJSON);
  EXPECT_EQ("remora", ReadDeviceRequisition(root.get()));
}

TEST_F(RequisitionUtilTest, BlankEnrollment) {
  std::unique_ptr<base::Value> root = JsonToUniquePtrValue(kNoEnrollmentJSON);
  EXPECT_EQ("", ReadDeviceRequisition(root.get()));
}

}  // namespace chromeos_update_engine
