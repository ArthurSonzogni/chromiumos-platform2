// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/commands/unittest_utils.h"

#include <base/json/json_reader.h>
#include <base/json/json_writer.h>

namespace buffet {
namespace unittests {

std::unique_ptr<base::Value> CreateValue(const char* json) {
  std::string json2(json);
  // Convert apostrophes to double-quotes so JSONReader can parse the string.
  std::replace(json2.begin(), json2.end(), '\'', '"');
  int error = 0;
  std::string message;
  std::unique_ptr<base::Value> value{base::JSONReader::ReadAndReturnError(
      json2, base::JSON_PARSE_RFC, &error, &message)};
  CHECK(value) << "Failed to load JSON: " << message << ", " << json;
  return value;
}

std::unique_ptr<base::DictionaryValue> CreateDictionaryValue(const char* json) {
  std::unique_ptr<base::Value> value = CreateValue(json);
  base::DictionaryValue* dict = nullptr;
  value->GetAsDictionary(&dict);
  CHECK(dict) << "Value is not dictionary: " << json;
  value.release();
  return std::unique_ptr<base::DictionaryValue>(dict);
}

}  // namespace unittests
}  // namespace buffet
