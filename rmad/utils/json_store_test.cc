// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_string_value_serializer.h>
#include <base/values.h>
#include <gtest/gtest.h>

#include "rmad/utils/json_store.h"

namespace rmad {

// File name.
const char kTestFileName[] = "test.json";

// Valid JSON dictionary.
const char kValidJson[] = R"(
  {
    "trigger": true,
    "state": "RMAD_STATE_RMA_NOT_REQUIRED",
    "replaced_components": [
      "screen",
      "keyboard"
    ]
  })";
// Invalid JSON string, missing '}'.
const char kInvalidFormatJson[] = "{ \"trigger\": true";
// Invalid JSON dictionary.
const char kWrongTypeJson[] = "[1, 2]";

const char kExistingKey[] = "trigger";
const bool kExistingValue = true;
const char kNewKey[] = "NewKey";
const int kNewValue = 10;

class JsonStoreTest : public testing::Test {
 public:
  JsonStoreTest() {}

  base::FilePath CreateInputFile(std::string file_name,
                                 const char* str,
                                 int size) {
    base::FilePath file_path = temp_dir_.GetPath().AppendASCII(file_name);
    base::WriteFile(file_path, str, size);
    return file_path;
  }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

TEST_F(JsonStoreTest, InitializeNormal) {
  base::FilePath input_file =
      CreateInputFile(kTestFileName, kValidJson, std::size(kValidJson) - 1);
  JsonStore json_store(input_file);
  EXPECT_EQ(json_store.GetReadError(), JsonStore::READ_ERROR_NONE);
  EXPECT_FALSE(json_store.ReadOnly());

  JSONStringValueDeserializer deserializer(kValidJson);
  int error_code;
  std::string error_message;
  std::unique_ptr<base::Value> expected_value =
      deserializer.Deserialize(&error_code, &error_message);
  EXPECT_EQ(json_store.GetValues(), *expected_value);
}

TEST_F(JsonStoreTest, InitializeInvalidString) {
  base::FilePath input_file = CreateInputFile(
      kTestFileName, kInvalidFormatJson, std::size(kInvalidFormatJson) - 1);
  JsonStore json_store(input_file);
  EXPECT_EQ(json_store.GetReadError(), JsonStore::READ_ERROR_JSON_PARSE);
  EXPECT_TRUE(json_store.ReadOnly());
  EXPECT_EQ(json_store.GetValues(), base::Value(base::Value::Type::DICTIONARY));
}

TEST_F(JsonStoreTest, InitializeInvalidType) {
  base::FilePath input_file = CreateInputFile(kTestFileName, kWrongTypeJson,
                                              std::size(kWrongTypeJson) - 1);
  JsonStore json_store(input_file);
  EXPECT_EQ(json_store.GetReadError(), JsonStore::READ_ERROR_JSON_TYPE);
  EXPECT_TRUE(json_store.ReadOnly());
  EXPECT_EQ(json_store.GetValues(), base::Value(base::Value::Type::DICTIONARY));
}

TEST_F(JsonStoreTest, InitializeNoFile) {
  base::FilePath input_file = temp_dir_.GetPath().AppendASCII(kTestFileName);
  JsonStore json_store(input_file);
  EXPECT_EQ(json_store.GetReadError(), JsonStore::READ_ERROR_NO_SUCH_FILE);
  EXPECT_FALSE(json_store.ReadOnly());
  EXPECT_EQ(json_store.GetValues(), base::Value(base::Value::Type::DICTIONARY));
}

TEST_F(JsonStoreTest, GetValue) {
  base::FilePath input_file =
      CreateInputFile(kTestFileName, kValidJson, std::size(kValidJson) - 1);
  JsonStore json_store(input_file);
  // Get by const pointer.
  const base::Value* value_ptr;
  EXPECT_FALSE(json_store.GetValue(kNewKey, &value_ptr));
  EXPECT_TRUE(json_store.GetValue(kExistingKey, &value_ptr));
  EXPECT_EQ(*value_ptr, base::Value(kExistingValue));
  // Get by copy.
  base::Value value;
  EXPECT_FALSE(json_store.GetValue(kNewKey, &value));
  EXPECT_TRUE(json_store.GetValue(kExistingKey, &value));
  EXPECT_EQ(value, base::Value(kExistingValue));
}

TEST_F(JsonStoreTest, SetValue) {
  base::FilePath input_file =
      CreateInputFile(kTestFileName, kValidJson, std::size(kValidJson) - 1);
  JsonStore json_store(input_file);
  base::Value value;
  // Add new key.
  EXPECT_FALSE(json_store.GetValue(kNewKey, &value));
  EXPECT_TRUE(json_store.SetValue(kNewKey, base::Value(kNewValue)));
  EXPECT_TRUE(json_store.GetValue(kNewKey, &value));
  EXPECT_EQ(value, base::Value(kNewValue));
  // Overwrite existing key.
  EXPECT_TRUE(json_store.GetValue(kExistingKey, &value));
  EXPECT_EQ(value, base::Value(kExistingValue));
  EXPECT_NE(base::Value(kExistingValue), base::Value(kNewValue));
  EXPECT_TRUE(json_store.SetValue(kExistingKey, base::Value(kNewValue)));
  EXPECT_TRUE(json_store.GetValue(kExistingKey, &value));
  EXPECT_EQ(value, base::Value(kNewValue));
}

TEST_F(JsonStoreTest, StoreValue) {
  base::FilePath input_file =
      CreateInputFile(kTestFileName, kValidJson, std::size(kValidJson) - 1);
  JsonStore json_store(input_file);
  base::Value value;
  // Add new key.
  EXPECT_FALSE(json_store.GetValue(kNewKey, &value));
  EXPECT_TRUE(json_store.SetValue(kNewKey, base::Value(kNewValue)));
  // Create a new JsonStore that reads the same file.
  JsonStore json_store_new(input_file);
  EXPECT_TRUE(json_store_new.GetValue(kNewKey, &value));
  EXPECT_EQ(value, base::Value(kNewValue));
}

}  // namespace rmad
