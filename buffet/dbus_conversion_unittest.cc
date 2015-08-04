// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/dbus_conversion.h"

#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <base/guid.h>
#include <base/rand_util.h>
#include <base/values.h>
#include <chromeos/variant_dictionary.h>
#include <gtest/gtest.h>
#include <weave/unittest_utils.h>

namespace buffet {

namespace {

using chromeos::Any;
using chromeos::VariantDictionary;
using weave::unittests::CreateDictionaryValue;
using weave::unittests::IsEqualValue;

chromeos::VariantDictionary ToDBus(const base::DictionaryValue& object) {
  return DictionaryToDBusVariantDictionary(object);
}

std::unique_ptr<base::DictionaryValue> FromDBus(
    const chromeos::VariantDictionary& object) {
  chromeos::ErrorPtr error;
  auto result = DictionaryFromDBusVariantDictionary(object, &error);
  EXPECT_TRUE(result || error);
  return result;
}

std::unique_ptr<base::Value> CreateRandomValue(int children);
std::unique_ptr<base::Value> CreateRandomValue(int children,
                                               base::Value::Type type);

const base::Value::Type kRandomTypes[] = {
    base::Value::TYPE_BOOLEAN,    base::Value::TYPE_INTEGER,
    base::Value::TYPE_DOUBLE,     base::Value::TYPE_STRING,
    base::Value::TYPE_DICTIONARY, base::Value::TYPE_LIST,
};

const base::Value::Type kRandomTypesWithChildren[] = {
    base::Value::TYPE_DICTIONARY, base::Value::TYPE_LIST,
};

base::Value::Type CreateRandomValueType(bool with_children) {
  if (with_children) {
    return kRandomTypesWithChildren[base::RandInt(
        0, arraysize(kRandomTypesWithChildren) - 1)];
  }
  return kRandomTypes[base::RandInt(0, arraysize(kRandomTypes) - 1)];
}

std::unique_ptr<base::DictionaryValue> CreateRandomDictionary(int children) {
  std::unique_ptr<base::DictionaryValue> result{new base::DictionaryValue};

  while (children > 0) {
    int sub_children = base::RandInt(1, children);
    children -= sub_children;
    result->Set(base::GenerateGUID(),
                CreateRandomValue(sub_children).release());
  }

  return result;
}

std::unique_ptr<base::ListValue> CreateRandomList(int children) {
  std::unique_ptr<base::ListValue> result{new base::ListValue};

  base::Value::Type type = CreateRandomValueType(children > 0);
  while (children > 0) {
    size_t max_children =
        (type != base::Value::TYPE_DICTIONARY && type != base::Value::TYPE_LIST)
            ? 1
            : children;
    size_t sub_children = base::RandInt(1, max_children);
    children -= sub_children;
    result->Append(CreateRandomValue(sub_children, type).release());
  }

  return result;
}

std::unique_ptr<base::Value> CreateRandomValue(int children,
                                               base::Value::Type type) {
  CHECK_GE(children, 1);
  switch (type) {
    case base::Value::TYPE_INTEGER:
      return std::unique_ptr<base::Value>{new base::FundamentalValue{
          base::RandInt(std::numeric_limits<int>::min(),
                        std::numeric_limits<int>::max())}};
    case base::Value::TYPE_DOUBLE:
      return std::unique_ptr<base::Value>{
          new base::FundamentalValue{base::RandDouble()}};
    case base::Value::TYPE_STRING:
      return std::unique_ptr<base::Value>{
          new base::StringValue{base::GenerateGUID()}};
    case base::Value::TYPE_DICTIONARY:
      CHECK_GE(children, 1);
      return CreateRandomDictionary(children - 1);
    case base::Value::TYPE_LIST:
      CHECK_GE(children, 1);
      return CreateRandomList(children - 1);
    default:
      return std::unique_ptr<base::Value>{
          new base::FundamentalValue{base::RandInt(0, 1) != 0}};
  }
}

std::unique_ptr<base::Value> CreateRandomValue(int children) {
  return CreateRandomValue(children, CreateRandomValueType(children > 0));
}

}  // namespace

TEST(DBusConversionTest, DictionaryToDBusVariantDictionary) {
  EXPECT_EQ((VariantDictionary{{"bool", true}}),
            ToDBus(*CreateDictionaryValue("{'bool': true}")));
  EXPECT_EQ((VariantDictionary{{"int", 5}}),
            ToDBus(*CreateDictionaryValue("{'int': 5}")));
  EXPECT_EQ((VariantDictionary{{"double", 6.7}}),
            ToDBus(*CreateDictionaryValue("{'double': 6.7}")));
  EXPECT_EQ((VariantDictionary{{"string", std::string{"abc"}}}),
            ToDBus(*CreateDictionaryValue("{'string': 'abc'}")));
  EXPECT_EQ((VariantDictionary{{"object", VariantDictionary{{"bool", true}}}}),
            ToDBus(*CreateDictionaryValue("{'object': {'bool': true}}")));
  EXPECT_EQ((VariantDictionary{{"emptyList", std::vector<Any>{}}}),
            ToDBus(*CreateDictionaryValue("{'emptyList': []}")));
  EXPECT_EQ((VariantDictionary{{"intList", std::vector<int>{5}}}),
            ToDBus(*CreateDictionaryValue("{'intList': [5]}")));
  EXPECT_EQ((VariantDictionary{
                {"intListList", std::vector<Any>{std::vector<int>{5},
                                                 std::vector<int>{6, 7}}}}),
            ToDBus(*CreateDictionaryValue("{'intListList': [[5], [6, 7]]}")));
  EXPECT_EQ((VariantDictionary{{"objList",
                                std::vector<VariantDictionary>{
                                    {{"string", std::string{"abc"}}}}}}),
            ToDBus(*CreateDictionaryValue("{'objList': [{'string': 'abc'}]}")));
}

TEST(DBusConversionTest, DictionaryFromDBusVariantDictionary) {
  EXPECT_JSON_EQ("{'bool': true}", *FromDBus({{"bool", true}}));
  EXPECT_JSON_EQ("{'int': 5}", *FromDBus({{"int", 5}}));
  EXPECT_JSON_EQ("{'double': 6.7}", *FromDBus({{"double", 6.7}}));
  EXPECT_JSON_EQ("{'string': 'abc'}",
                 *FromDBus({{"string", std::string{"abc"}}}));
  EXPECT_JSON_EQ("{'object': {'bool': true}}",
                 *FromDBus({{"object", VariantDictionary{{"bool", true}}}}));
  EXPECT_JSON_EQ("{'emptyList': []}",
                 *FromDBus({{"emptyList", std::vector<bool>{}}}));
  EXPECT_JSON_EQ("{'intList': [5]}",
                 *FromDBus({{"intList", std::vector<int>{5}}}));
  EXPECT_JSON_EQ(
      "{'intListList': [[5], [6, 7]]}",
      *FromDBus({{"intListList", std::vector<Any>{std::vector<int>{5},
                                                  std::vector<int>{6, 7}}}}));
  EXPECT_JSON_EQ(
      "{'objList': [{'string': 'abc'}]}",
      *FromDBus({{"objList", std::vector<VariantDictionary>{
                                 {{"string", std::string{"abc"}}}}}}));
  EXPECT_JSON_EQ("{'int': 5}", *FromDBus({{"int", Any{Any{5}}}}));
}

TEST(DBusConversionTest, DictionaryFromDBusVariantDictionary_Errors) {
  EXPECT_FALSE(FromDBus({{"cString", "abc"}}));
  EXPECT_FALSE(FromDBus({{"float", 1.0f}}));
  EXPECT_FALSE(FromDBus({{"listList", std::vector<std::vector<int>>{}}}));
  EXPECT_FALSE(FromDBus({{"any", Any{}}}));
  EXPECT_FALSE(FromDBus({{"null", nullptr}}));
}

TEST(DBusConversionTest, DBusRandomDictionaryConversion) {
  auto dict = CreateRandomDictionary(10000);
  auto varian_dict = ToDBus(*dict);
  auto dict_restored = FromDBus(varian_dict);
  EXPECT_PRED2(IsEqualValue, *dict, *dict_restored);
}

}  // namespace buffet
