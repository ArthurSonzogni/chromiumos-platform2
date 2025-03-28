// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/dbus/data_serialization.h>

#include <unistd.h>

#include <limits>
#include <tuple>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <brillo/variant_dictionary.h>
#include <gtest/gtest.h>

#include "brillo/dbus/test.pb.h"

using dbus::Message;
using dbus::MessageReader;
using dbus::MessageWriter;
using dbus::ObjectPath;
using dbus::Response;

namespace brillo {
namespace dbus_utils {
namespace {
class ScopedAutoVariantUnwrapState {
 public:
  explicit ScopedAutoVariantUnwrapState(AutoVariantUnwrapState state)
      : original_state_(GetAutoVariantUnwrapState()) {
    SetAutoVariantUnwrapState(state);
  }
  ~ScopedAutoVariantUnwrapState() {
    SetAutoVariantUnwrapState(original_state_);
  }

 private:
  AutoVariantUnwrapState original_state_;
};
}  // namespace

TEST(DBusUtils, Supported_BasicTypes) {
  EXPECT_TRUE(IsTypeSupported<bool>::value);
  EXPECT_TRUE(IsTypeSupported<uint8_t>::value);
  EXPECT_TRUE(IsTypeSupported<int16_t>::value);
  EXPECT_TRUE(IsTypeSupported<uint16_t>::value);
  EXPECT_TRUE(IsTypeSupported<int32_t>::value);
  EXPECT_TRUE(IsTypeSupported<uint32_t>::value);
  EXPECT_TRUE(IsTypeSupported<int64_t>::value);
  EXPECT_TRUE(IsTypeSupported<uint64_t>::value);
  EXPECT_TRUE(IsTypeSupported<double>::value);
  EXPECT_TRUE(IsTypeSupported<std::string>::value);
  EXPECT_TRUE(IsTypeSupported<ObjectPath>::value);
  EXPECT_TRUE(IsTypeSupported<base::ScopedFD>::value);
  EXPECT_TRUE(IsTypeSupported<Any>::value);
  EXPECT_TRUE(IsTypeSupported<google::protobuf::MessageLite>::value);
  EXPECT_TRUE(IsTypeSupported<dbus_utils_test::TestMessage>::value);
}

TEST(DBusUtils, Unsupported_BasicTypes) {
  EXPECT_FALSE(IsTypeSupported<char>::value);
  EXPECT_FALSE(IsTypeSupported<float>::value);
}

TEST(DBusUtils, Supported_ComplexTypes) {
  EXPECT_TRUE(IsTypeSupported<std::vector<bool>>::value);
  EXPECT_TRUE(IsTypeSupported<std::vector<uint8_t>>::value);
  EXPECT_TRUE((IsTypeSupported<std::pair<int16_t, double>>::value));
  EXPECT_TRUE(
      (IsTypeSupported<std::map<uint16_t, std::vector<int64_t>>>::value));
  EXPECT_TRUE((IsTypeSupported<std::tuple<bool, double, int32_t>>::value));
  EXPECT_TRUE(
      IsTypeSupported<std::vector<dbus_utils_test::TestMessage>>::value);
}

TEST(DBusUtils, Unsupported_ComplexTypes) {
  EXPECT_FALSE(IsTypeSupported<std::vector<char>>::value);
  EXPECT_FALSE((IsTypeSupported<std::pair<int16_t, float>>::value));
  EXPECT_FALSE((IsTypeSupported<std::pair<char, int32_t>>::value));
  EXPECT_FALSE((IsTypeSupported<std::map<int16_t, float>>::value));
  EXPECT_FALSE((IsTypeSupported<std::map<char, int32_t>>::value));
  EXPECT_FALSE((IsTypeSupported<std::tuple<bool, char, int32_t>>::value));
}

TEST(DBusUtils, Supported_TypeSet) {
  EXPECT_TRUE((IsTypeSupported<int32_t, double, std::string>::value));
  EXPECT_TRUE((IsTypeSupported<bool, std::vector<int32_t>, uint8_t>::value));
}

TEST(DBusUtils, Unupported_TypeSet) {
  EXPECT_FALSE((IsTypeSupported<int32_t, double, std::string, char>::value));
  EXPECT_FALSE(
      (IsTypeSupported<bool, std::pair<std::vector<float>, uint8_t>>::value));
  EXPECT_FALSE((IsTypeSupported<char, double, std::string, int16_t>::value));
  EXPECT_FALSE((IsTypeSupported<char, std::vector<float>, float>::value));
}

// Test that a byte can be properly written and read. We only have this
// test for byte, as repeating this for other basic types is too redundant.
TEST(DBusUtils, AppendAndPopByte) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  AppendValueToWriter(&writer, uint8_t{123});
  EXPECT_EQ("y", message->GetSignature());

  MessageReader reader(message.get());
  EXPECT_TRUE(reader.HasMoreData());  // Should have data to read.
  EXPECT_EQ(Message::BYTE, reader.GetDataType());

  bool bool_value = false;
  // Should fail as the type is not bool here.
  EXPECT_FALSE(PopValueFromReader(&reader, &bool_value));

  uint8_t byte_value = 0;
  EXPECT_TRUE(PopValueFromReader(&reader, &byte_value));
  EXPECT_EQ(123, byte_value);          // Should match with the input.
  EXPECT_FALSE(reader.HasMoreData());  // Should not have more data to read.

  // Try to get another byte. Should fail.
  EXPECT_FALSE(PopValueFromReader(&reader, &byte_value));
}

// Check all basic types can be properly written and read.
TEST(DBusUtils, AppendAndPopBasicDataTypes) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());

  // Append 0, true, 2, 3, 4, 5, 6, 7, 8.0, "string", "/object/path".
  AppendValueToWriter(&writer, uint8_t{0});
  AppendValueToWriter(&writer, bool{true});
  AppendValueToWriter(&writer, int16_t{2});
  AppendValueToWriter(&writer, uint16_t{3});
  AppendValueToWriter(&writer, int32_t{4});
  AppendValueToWriter(&writer, uint32_t{5});
  AppendValueToWriter(&writer, int64_t{6});
  AppendValueToWriter(&writer, uint64_t{7});
  AppendValueToWriter(&writer, double{8.0});
  AppendValueToWriter(&writer, std::string{"string"});
  AppendValueToWriter(&writer, ObjectPath{"/object/path"});

  EXPECT_EQ("ybnqiuxtdso", message->GetSignature());

  uint8_t byte_value = 0;
  bool bool_value = false;
  int16_t int16_value = 0;
  uint16_t uint16_value = 0;
  int32_t int32_value = 0;
  uint32_t uint32_value = 0;
  int64_t int64_value = 0;
  uint64_t uint64_value = 0;
  double double_value = 0;
  std::string string_value;
  ObjectPath object_path_value;

  MessageReader reader(message.get());
  EXPECT_TRUE(reader.HasMoreData());
  EXPECT_TRUE(PopValueFromReader(&reader, &byte_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &bool_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &int16_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &uint16_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &int32_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &uint32_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &int64_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &uint64_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &double_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &string_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &object_path_value));
  EXPECT_FALSE(reader.HasMoreData());

  // 0, true, 2, 3, 4, 5, 6, 7, 8, "string", "/object/path" should be returned.
  EXPECT_EQ(0, byte_value);
  EXPECT_TRUE(bool_value);
  EXPECT_EQ(2, int16_value);
  EXPECT_EQ(3U, uint16_value);
  EXPECT_EQ(4, int32_value);
  EXPECT_EQ(5U, uint32_value);
  EXPECT_EQ(6, int64_value);
  EXPECT_EQ(7U, uint64_value);
  EXPECT_DOUBLE_EQ(8.0, double_value);
  EXPECT_EQ("string", string_value);
  EXPECT_EQ(ObjectPath{"/object/path"}, object_path_value);
}

// Check all basic types can be properly written and read.
TEST(DBusUtils, AppendAndPopFileDescriptor) {
  if (!dbus::IsDBusTypeUnixFdSupported()) {
    LOG(WARNING) << "FD passing is not supported";
    return;
  }

  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());

  // Append stdout.
  base::ScopedFD temp(dup(1));
  AppendValueToWriter(&writer, temp);

  EXPECT_EQ("h", message->GetSignature());

  base::ScopedFD fd_value;

  MessageReader reader(message.get());
  EXPECT_TRUE(reader.HasMoreData());
  EXPECT_TRUE(PopValueFromReader(&reader, &fd_value));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_TRUE(fd_value.is_valid());
}

// Check all variant types can be properly written and read.
TEST(DBusUtils, AppendAndPopVariantDataTypes) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());

  // Append 10, false, 12, 13, 14, 15, 16, 17, 18.5, "data", "/obj/path".
  AppendValueToWriterAsVariant(&writer, uint8_t{10});
  AppendValueToWriterAsVariant(&writer, bool{false});
  AppendValueToWriterAsVariant(&writer, int16_t{12});
  AppendValueToWriterAsVariant(&writer, uint16_t{13});
  AppendValueToWriterAsVariant(&writer, int32_t{14});
  AppendValueToWriterAsVariant(&writer, uint32_t{15});
  AppendValueToWriterAsVariant(&writer, int64_t{16});
  AppendValueToWriterAsVariant(&writer, uint64_t{17});
  AppendValueToWriterAsVariant(&writer, double{18.5});
  AppendValueToWriterAsVariant(&writer, std::string{"data"});
  AppendValueToWriterAsVariant(&writer, ObjectPath{"/obj/path"});
  AppendValueToWriterAsVariant(&writer, Any{17});
  AppendValueToWriterAsVariant(&writer,
                               Any{std::vector<std::vector<int>>{{6, 7}}});

  EXPECT_EQ("vvvvvvvvvvvvv", message->GetSignature());

  uint8_t byte_value = 0;
  bool bool_value = true;
  int16_t int16_value = 0;
  uint16_t uint16_value = 0;
  int32_t int32_value = 0;
  uint32_t uint32_value = 0;
  int64_t int64_value = 0;
  uint64_t uint64_value = 0;
  double double_value = 0;
  std::string string_value;
  ObjectPath object_path_value;
  Any any_value;
  Any any_vector_vector;

  MessageReader reader(message.get());
  EXPECT_TRUE(reader.HasMoreData());
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &byte_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &bool_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &int16_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &uint16_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &int32_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &uint32_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &int64_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &uint64_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &double_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &string_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &object_path_value));
  EXPECT_TRUE(PopVariantValueFromReader(&reader, &any_value));
  // Not implemented.
  EXPECT_FALSE(PopVariantValueFromReader(&reader, &any_vector_vector));
  EXPECT_FALSE(reader.HasMoreData());

  EXPECT_EQ(10, byte_value);
  EXPECT_FALSE(bool_value);
  EXPECT_EQ(12, int16_value);
  EXPECT_EQ(13U, uint16_value);
  EXPECT_EQ(14, int32_value);
  EXPECT_EQ(15U, uint32_value);
  EXPECT_EQ(16, int64_value);
  EXPECT_EQ(17U, uint64_value);
  EXPECT_DOUBLE_EQ(18.5, double_value);
  EXPECT_EQ("data", string_value);
  EXPECT_EQ(ObjectPath{"/obj/path"}, object_path_value);
  EXPECT_EQ(17, any_value.Get<int>());
  EXPECT_TRUE(any_vector_vector.IsEmpty());
}

// Check that empty variant data is handled without crashing. The scenario
// happens when a D-Bus method with variant type input is called without passing
// any arguments.
TEST(DBusUtils, AppendAndPopEmptyVariantDataType) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageReader reader(message.get());

  Any any_value;
  EXPECT_FALSE(PopVariantValueFromReader(&reader, &any_value));
}

TEST(DBusUtils, AppendAndPopBasicAny) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());

  // Append 10, true, 12, 13, 14, 15, 16, 17, 18.5, "data", "/obj/path".
  AppendValueToWriter(&writer, Any(uint8_t{10}));
  AppendValueToWriter(&writer, Any(bool{true}));
  AppendValueToWriter(&writer, Any(int16_t{12}));
  AppendValueToWriter(&writer, Any(uint16_t{13}));
  AppendValueToWriter(&writer, Any(int32_t{14}));
  AppendValueToWriter(&writer, Any(uint32_t{15}));
  AppendValueToWriter(&writer, Any(int64_t{16}));
  AppendValueToWriter(&writer, Any(uint64_t{17}));
  AppendValueToWriter(&writer, Any(double{18.5}));
  AppendValueToWriter(&writer, Any(std::string{"data"}));
  AppendValueToWriter(&writer, Any(ObjectPath{"/obj/path"}));
  EXPECT_EQ("vvvvvvvvvvv", message->GetSignature());

  Any byte_value;
  Any bool_value;
  Any int16_value;
  Any uint16_value;
  Any int32_value;
  Any uint32_value;
  Any int64_value;
  Any uint64_value;
  Any double_value;
  Any string_value;
  Any object_path_value;

  MessageReader reader(message.get());
  EXPECT_TRUE(reader.HasMoreData());
  EXPECT_TRUE(PopValueFromReader(&reader, &byte_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &bool_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &int16_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &uint16_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &int32_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &uint32_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &int64_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &uint64_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &double_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &string_value));
  EXPECT_TRUE(PopValueFromReader(&reader, &object_path_value));
  EXPECT_FALSE(reader.HasMoreData());

  // Must be: 10, true, 12, 13, 14, 15, 16, 17, 18.5, "data", "/obj/path".
  EXPECT_EQ(10, byte_value.Get<uint8_t>());
  EXPECT_TRUE(bool_value.Get<bool>());
  EXPECT_EQ(12, int16_value.Get<int16_t>());
  EXPECT_EQ(13U, uint16_value.Get<uint16_t>());
  EXPECT_EQ(14, int32_value.Get<int32_t>());
  EXPECT_EQ(15U, uint32_value.Get<uint32_t>());
  EXPECT_EQ(16, int64_value.Get<int64_t>());
  EXPECT_EQ(17U, uint64_value.Get<uint64_t>());
  EXPECT_DOUBLE_EQ(18.5, double_value.Get<double>());
  EXPECT_EQ("data", string_value.Get<std::string>());
  EXPECT_EQ(ObjectPath{"/obj/path"}, object_path_value.Get<ObjectPath>());
}

TEST(DBusUtils, ArrayOfBytes) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  std::vector<uint8_t> bytes{1, 2, 3};
  AppendValueToWriter(&writer, bytes);

  EXPECT_EQ("ay", message->GetSignature());

  MessageReader reader(message.get());
  std::vector<uint8_t> bytes_out;
  EXPECT_TRUE(PopValueFromReader(&reader, &bytes_out));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_EQ(bytes, bytes_out);
}

TEST(DBusUtils, ArrayOfBytes_Empty) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  std::vector<uint8_t> bytes;
  AppendValueToWriter(&writer, bytes);

  EXPECT_EQ("ay", message->GetSignature());

  MessageReader reader(message.get());
  std::vector<uint8_t> bytes_out;
  EXPECT_TRUE(PopValueFromReader(&reader, &bytes_out));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_EQ(bytes, bytes_out);
}

TEST(DBusUtils, ArrayOfStrings) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  std::vector<std::string> strings{"foo", "bar", "baz"};
  AppendValueToWriter(&writer, strings);

  EXPECT_EQ("as", message->GetSignature());

  MessageReader reader(message.get());
  std::vector<std::string> strings_out;
  EXPECT_TRUE(PopValueFromReader(&reader, &strings_out));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_EQ(strings, strings_out);
}

TEST(DBusUtils, ArrayOfInt64) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  std::vector<int64_t> values{-5,
                              -4,
                              -3,
                              -2,
                              -1,
                              0,
                              1,
                              2,
                              3,
                              4,
                              5,
                              std::numeric_limits<int64_t>::min(),
                              std::numeric_limits<int64_t>::max()};
  AppendValueToWriter(&writer, values);

  EXPECT_EQ("ax", message->GetSignature());

  MessageReader reader(message.get());
  std::vector<int64_t> values_out;
  EXPECT_TRUE(PopValueFromReader(&reader, &values_out));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_EQ(values, values_out);
}

TEST(DBusUtils, ArrayOfObjectPaths) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  std::vector<ObjectPath> object_paths{
      ObjectPath("/object/path/1"),
      ObjectPath("/object/path/2"),
      ObjectPath("/object/path/3"),
  };
  AppendValueToWriter(&writer, object_paths);

  EXPECT_EQ("ao", message->GetSignature());

  MessageReader reader(message.get());
  std::vector<ObjectPath> object_paths_out;
  EXPECT_TRUE(PopValueFromReader(&reader, &object_paths_out));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_EQ(object_paths, object_paths_out);
}

TEST(DBusUtils, ArraysAsVariant) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  std::vector<int> int_array{1, 2, 3};
  std::vector<std::string> str_array{"foo", "bar", "baz"};
  std::vector<double> dbl_array_empty{};
  std::map<std::string, std::string> dict_ss{{"k1", "v1"}, {"k2", "v2"}};
  VariantDictionary dict_sv{{"k1", 1}, {"k2", "v2"}};
  using ComplexStructArray =
      std::vector<std::tuple<uint32_t, bool, std::vector<uint8_t>>>;
  ComplexStructArray complex_struct_array{
      {123, true, {0xaa, 0xbb, 0xcc}},
      {456, false, {0xdd}},
      {789, false, {}},
  };
  AppendValueToWriterAsVariant(&writer, int_array);
  AppendValueToWriterAsVariant(&writer, str_array);
  AppendValueToWriterAsVariant(&writer, dbl_array_empty);
  AppendValueToWriterAsVariant(&writer, dict_ss);
  AppendValueToWriterAsVariant(&writer, dict_sv);
  AppendValueToWriterAsVariant(&writer, complex_struct_array);

  EXPECT_EQ("vvvvvv", message->GetSignature());

  Any int_array_out;
  Any str_array_out;
  Any dbl_array_out;
  Any dict_ss_out;
  Any dict_sv_out;
  Any complex_struct_array_out;

  MessageReader reader(message.get());
  EXPECT_TRUE(PopValueFromReader(&reader, &int_array_out));
  EXPECT_TRUE(PopValueFromReader(&reader, &str_array_out));
  EXPECT_TRUE(PopValueFromReader(&reader, &dbl_array_out));
  EXPECT_TRUE(PopValueFromReader(&reader, &dict_ss_out));
  EXPECT_TRUE(PopValueFromReader(&reader, &dict_sv_out));
  EXPECT_TRUE(PopValueFromReader(&reader, &complex_struct_array_out));
  EXPECT_FALSE(reader.HasMoreData());

  EXPECT_EQ(int_array, int_array_out.Get<std::vector<int>>());
  EXPECT_EQ(str_array, str_array_out.Get<std::vector<std::string>>());
  EXPECT_EQ(dbl_array_empty, dbl_array_out.Get<std::vector<double>>());
  EXPECT_EQ(dict_ss, (dict_ss_out.Get<std::map<std::string, std::string>>()));
  EXPECT_EQ(dict_sv["k1"].Get<int>(),
            dict_sv_out.Get<VariantDictionary>().at("k1").Get<int>());
  EXPECT_EQ(dict_sv["k2"].Get<const char*>(),
            dict_sv_out.Get<VariantDictionary>().at("k2").Get<std::string>());
  EXPECT_EQ(complex_struct_array,
            complex_struct_array_out.Get<ComplexStructArray>());
}

TEST(DBusUtils, StructsAsVariant) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  VariantDictionary dict_sv{{"k1", 1}, {"k2", "v2"}};
  std::tuple<uint32_t, VariantDictionary> u32_dict_sv_struct =
      std::make_tuple(1, dict_sv);
  AppendValueToWriterAsVariant(&writer, u32_dict_sv_struct);

  EXPECT_EQ("v", message->GetSignature());

  Any u32_dict_sv_struct_out_any;

  MessageReader reader(message.get());
  EXPECT_TRUE(PopValueFromReader(&reader, &u32_dict_sv_struct_out_any));
  EXPECT_FALSE(reader.HasMoreData());

  auto u32_dict_sv_struct_out =
      u32_dict_sv_struct_out_any.Get<std::tuple<uint32_t, VariantDictionary>>();
  EXPECT_EQ(std::get<0>(u32_dict_sv_struct),
            std::get<0>(u32_dict_sv_struct_out));
  VariantDictionary dict_sv_out = std::get<1>(u32_dict_sv_struct_out);
  EXPECT_EQ(dict_sv.size(), dict_sv_out.size());
  EXPECT_EQ(dict_sv["k1"].Get<int>(), dict_sv_out["k1"].Get<int>());
  EXPECT_EQ(dict_sv["k2"].Get<const char*>(),
            dict_sv_out["k2"].Get<std::string>());
}

TEST(DBusUtils, VariantDictionary) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  VariantDictionary values{
      {"key1", uint8_t{10}},
      {"key2", bool{true}},
      {"key3", int16_t{12}},
      {"key4", uint16_t{13}},
      {"key5", int32_t{14}},
      {"key6", uint32_t{15}},
      {"key7", int64_t{16}},
      {"key8", uint64_t{17}},
      {"key9", double{18.5}},
      {"keyA", std::string{"data"}},
      {"keyB", ObjectPath{"/obj/path"}},
  };
  AppendValueToWriter(&writer, values);

  EXPECT_EQ("a{sv}", message->GetSignature());

  MessageReader reader(message.get());
  VariantDictionary values_out;
  EXPECT_TRUE(PopValueFromReader(&reader, &values_out));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_EQ(values.size(), values_out.size());
  EXPECT_EQ(values["key1"].Get<uint8_t>(), values_out["key1"].Get<uint8_t>());
  EXPECT_EQ(values["key2"].Get<bool>(), values_out["key2"].Get<bool>());
  EXPECT_EQ(values["key3"].Get<int16_t>(), values_out["key3"].Get<int16_t>());
  EXPECT_EQ(values["key4"].Get<uint16_t>(), values_out["key4"].Get<uint16_t>());
  EXPECT_EQ(values["key5"].Get<int32_t>(), values_out["key5"].Get<int32_t>());
  EXPECT_EQ(values["key6"].Get<uint32_t>(), values_out["key6"].Get<uint32_t>());
  EXPECT_EQ(values["key7"].Get<int64_t>(), values_out["key7"].Get<int64_t>());
  EXPECT_EQ(values["key8"].Get<uint64_t>(), values_out["key8"].Get<uint64_t>());
  EXPECT_EQ(values["key9"].Get<double>(), values_out["key9"].Get<double>());
  EXPECT_EQ(values["keyA"].Get<std::string>(),
            values_out["keyA"].Get<std::string>());
  EXPECT_EQ(values["keyB"].Get<ObjectPath>(),
            values_out["keyB"].Get<ObjectPath>());
}

TEST(DBusUtils, StringToStringMap) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  std::map<std::string, std::string> values{
      {"key1", "value1"}, {"key2", "value2"}, {"key3", "value3"},
      {"key4", "value4"}, {"key5", "value5"},
  };
  AppendValueToWriter(&writer, values);

  EXPECT_EQ("a{ss}", message->GetSignature());

  MessageReader reader(message.get());
  std::map<std::string, std::string> values_out;
  EXPECT_TRUE(PopValueFromReader(&reader, &values_out));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_EQ(values, values_out);
}

TEST(DBusUtils, Pair) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  std::pair<std::string, int> struct1{"value2", 3};
  AppendValueToWriter(&writer, struct1);
  std::pair<int, std::pair<int, int>> struct2{1, {2, 3}};
  AppendValueToWriter(&writer, struct2);

  EXPECT_EQ("(si)(i(ii))", message->GetSignature());

  std::pair<std::string, int> struct1_out;
  std::pair<int, std::pair<int, int>> struct2_out;

  MessageReader reader(message.get());
  EXPECT_TRUE(PopValueFromReader(&reader, &struct1_out));
  EXPECT_TRUE(PopValueFromReader(&reader, &struct2_out));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_EQ(struct1, struct1_out);
  EXPECT_EQ(struct2, struct2_out);
}

TEST(DBusUtils, Tuple) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  std::tuple<std::string, int> struct1{"value2", 3};
  AppendValueToWriter(&writer, struct1);
  std::tuple<int, std::string, std::vector<std::pair<int, int>>> struct2{
      1,
      "a",
      {{2, 3}},
  };
  AppendValueToWriter(&writer, struct2);

  EXPECT_EQ("(si)(isa(ii))", message->GetSignature());

  std::tuple<std::string, int> struct1_out;
  std::tuple<int, std::string, std::vector<std::pair<int, int>>> struct2_out;

  MessageReader reader(message.get());
  EXPECT_TRUE(PopValueFromReader(&reader, &struct1_out));
  EXPECT_TRUE(PopValueFromReader(&reader, &struct2_out));
  EXPECT_FALSE(reader.HasMoreData());
  EXPECT_EQ(struct1, struct1_out);
  EXPECT_EQ(struct2, struct2_out);
}

TEST(DBusUtils, EmptyVariant) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  EXPECT_DEATH(AppendValueToWriter(&writer, Any{}),
               "Must not be called on an empty Any");
}

TEST(DBusUtils, IncompatibleVariant) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());
  EXPECT_DEATH(AppendValueToWriter(&writer, Any{2.2f}),
               "Type 'float' is not supported by D-Bus");
}

TEST(DBusUtils, Protobuf) {
  std::unique_ptr<Response> message = Response::CreateEmpty();
  MessageWriter writer(message.get());

  dbus_utils_test::TestMessage test_message;
  test_message.set_foo(123);
  test_message.set_bar("abcd");

  AppendValueToWriter(&writer, test_message);

  EXPECT_EQ("ay", message->GetSignature());

  dbus_utils_test::TestMessage test_message_out;

  MessageReader reader(message.get());
  EXPECT_TRUE(PopValueFromReader(&reader, &test_message_out));
  EXPECT_FALSE(reader.HasMoreData());

  EXPECT_EQ(123, test_message_out.foo());
  EXPECT_EQ("abcd", test_message_out.bar());
}

TEST(DBusUtils, ApplyReadDBusArgs) {
  // Test of storage tuple.
  {
    std::unique_ptr<Response> message = Response::CreateEmpty();
    MessageWriter writer(message.get());
    WriteDBusArgs(&writer, 1, true, 5.);
    ASSERT_EQ("ibd", message->GetSignature());

    MessageReader reader(message.get());

    std::tuple<std::int32_t, bool, double> tuple;
    ASSERT_TRUE(ApplyReadDBusArgs(&reader, tuple));
    EXPECT_EQ(1, std::get<0>(tuple));
    EXPECT_TRUE(std::get<1>(tuple));
    // Intentionally compare by == for double, we do not expect any FP error.
    EXPECT_EQ(5., std::get<2>(tuple));
  }

  // Test of reference tuple.
  {
    std::unique_ptr<Response> message = Response::CreateEmpty();
    MessageWriter writer(message.get());
    WriteDBusArgs(&writer, 1, true, 5.);
    ASSERT_EQ("ibd", message->GetSignature());

    MessageReader reader(message.get());

    std::int32_t i = 0;
    bool b = false;
    double d = 0.;
    ASSERT_TRUE(ApplyReadDBusArgs(&reader, std::tie(i, b, d)));
    EXPECT_EQ(1, i);
    EXPECT_TRUE(b);
    // Intentionally compare by == for double, we do not expect any FP error.
    EXPECT_EQ(5., d);
  }
}

}  // namespace dbus_utils
}  // namespace brillo
