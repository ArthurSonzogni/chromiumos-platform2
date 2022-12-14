// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libipp/ipp_attribute.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <string>
#include <vector>

#include "frame.h"  // needed for ipp::Code

namespace {

ipp::InternalType InternalTypeForUnknownAttribute(ipp::ValueTag type) {
  switch (type) {
    case ipp::ValueTag::collection:
      return ipp::InternalType::kCollection;
    case ipp::ValueTag::boolean:
    case ipp::ValueTag::integer:
    case ipp::ValueTag::enum_:
      return ipp::InternalType::kInteger;
    case ipp::ValueTag::dateTime:
      return ipp::InternalType::kDateTime;
    case ipp::ValueTag::resolution:
      return ipp::InternalType::kResolution;
    case ipp::ValueTag::rangeOfInteger:
      return ipp::InternalType::kRangeOfInteger;
    case ipp::ValueTag::nameWithLanguage:
    case ipp::ValueTag::textWithLanguage:
      return ipp::InternalType::kStringWithLanguage;
    default:
      return ipp::InternalType::kString;
  }
}

std::string UnsignedToString(size_t x) {
  std::string s;
  do {
    s.push_back('0' + (x % 10));
    x /= 10;
  } while (x > 0);
  std::reverse(s.begin(), s.end());
  return s;
}
}  // namespace

namespace ipp {

namespace {

// This struct exposes single static method performing conversion between values
// of different types. Returns true if conversion succeeded and false otherwise.
// |out_val| cannot be nullptr.
template <typename InputType, typename OutputType>
struct Converter {
  static bool Convert(const std::string& name,
                      const AttrDef& def,
                      const InputType& in_val,
                      OutputType* out_val) {
    return false;
  }
};
template <typename Type>
struct Converter<Type, Type> {
  static bool Convert(const std::string& name,
                      const AttrDef& def,
                      const Type& in_val,
                      Type* out_val) {
    *out_val = in_val;
    return true;
  }
};
template <>
struct Converter<std::string, std::string> {
  static bool Convert(const std::string& name,
                      const AttrDef& def,
                      const std::string& in_val,
                      std::string* out_val) {
    *out_val = in_val;
    return true;
  }
};
template <typename InputType>
struct Converter<InputType, std::string> {
  static bool Convert(const std::string& name,
                      const AttrDef& def,
                      const InputType& in_val,
                      std::string* out_val) {
    *out_val = ToString(in_val);
    return true;
  }
};
template <>
struct Converter<int32_t, std::string> {
  static bool Convert(const std::string& name,
                      const AttrDef& def,
                      int32_t in_val,
                      std::string* out_val) {
    if (def.ipp_type == ValueTag::boolean) {
      *out_val = ToString(static_cast<bool>(in_val));
    } else if (def.ipp_type == ValueTag::enum_ ||
               def.ipp_type == ValueTag::keyword) {
      AttrName attr_name;
      if (!FromString(name, &attr_name))
        return false;
      *out_val = ToString(attr_name, in_val);
    } else if (def.ipp_type == ValueTag::integer) {
      *out_val = ToString(in_val);
    } else {
      return false;
    }
    return true;
  }
};
template <>
struct Converter<std::string, bool> {
  static bool Convert(const std::string& name,
                      const AttrDef& def,
                      const std::string& in_val,
                      bool* out_val) {
    return FromString(in_val, out_val);
  }
};
template <>
struct Converter<std::string, int32_t> {
  static bool Convert(const std::string& name,
                      const AttrDef& def,
                      const std::string& in_val,
                      int32_t* out_val) {
    bool result = false;
    if (def.ipp_type == ValueTag::boolean) {
      bool out;
      result = FromString(in_val, &out);
      if (result)
        *out_val = out;
    } else if (def.ipp_type == ValueTag::enum_ ||
               def.ipp_type == ValueTag::keyword) {
      AttrName attr_name;
      if (!FromString(name, &attr_name))
        return false;
      int out;
      result = FromString(in_val, attr_name, &out);
      if (result)
        *out_val = out;
    } else if (def.ipp_type == ValueTag::integer) {
      int out;
      result = FromString(in_val, &out);
      if (result)
        *out_val = out;
    }
    return result;
  }
};
template <>
struct Converter<std::string, StringWithLanguage> {
  static bool Convert(const std::string& name,
                      const AttrDef& def,
                      const std::string& in_val,
                      StringWithLanguage* out_val) {
    out_val->language = "";
    out_val->value = in_val;
    return true;
  }
};

// Creates new value for attribute |def| and saves it as void*.
template <typename Type>
void* CreateValue(const AttrDef& def) {
  if (sizeof(Type) <= sizeof(void*) && alignof(Type) <= alignof(void*))
    return 0;
  return new Type();
}

// Deletes value saved as void*.
template <typename Type>
void DeleteValue(void* value) {
  if (sizeof(Type) <= sizeof(void*) && alignof(Type) <= alignof(void*))
    return;
  delete reinterpret_cast<Type*>(value);
}

// Returns pointer to a value stored as void*.
template <typename Type>
Type* ReadValuePtr(void** value) {
  if (sizeof(Type) <= sizeof(void*) && alignof(Type) <= alignof(void*))
    return reinterpret_cast<Type*>(value);
  return reinterpret_cast<Type*>(*value);
}

// Const version of the template function above.
template <typename Type>
const Type* ReadValueConstPtr(void* const* value) {
  if (sizeof(Type) <= sizeof(void*) && alignof(Type) <= alignof(void*))
    return reinterpret_cast<const Type*>(value);
  return reinterpret_cast<Type* const>(*value);
}

// Resizes vector of values in an attribute |def|.
template <typename Type>
void ResizeVector(const AttrDef& def, std::vector<Type>* v, size_t new_size) {
  v->resize(new_size);
}
template <>
void ResizeVector<Collection*>(const AttrDef& def,
                               std::vector<Collection*>* v,
                               size_t new_size) {
  const size_t old_size = v->size();
  for (size_t i = new_size; i < old_size; ++i)
    delete v->at(i);
  v->resize(new_size);
  for (size_t i = old_size; i < new_size; ++i)
    (*v)[i] = new Collection;
}

// Deletes the whole attribute's |values|.
template <typename Type>
void DeleteAttrTyped(void*& values, const AttrDef& def) {
  if (values == nullptr)
    return;
  auto pv = reinterpret_cast<std::vector<Type>*>(values);
  ResizeVector<Type>(def, pv, 0);
  delete pv;
  values = nullptr;
}

// The same as previous one, just chooses correct template instantiation.
void DeleteAttr(void*& values, const AttrDef& def) {
  switch (def.cc_type) {
    case InternalType::kInteger:
      DeleteAttrTyped<int32_t>(values, def);
      break;
    case InternalType::kString:
      DeleteAttrTyped<std::string>(values, def);
      break;
    case InternalType::kResolution:
      DeleteAttrTyped<Resolution>(values, def);
      break;
    case InternalType::kRangeOfInteger:
      DeleteAttrTyped<RangeOfInteger>(values, def);
      break;
    case InternalType::kDateTime:
      DeleteAttrTyped<DateTime>(values, def);
      break;
    case InternalType::kStringWithLanguage:
      DeleteAttrTyped<StringWithLanguage>(values, def);
      break;
    case InternalType::kCollection:
      DeleteAttrTyped<Collection*>(values, def);
      break;
  }
}

// Returns a pointer to a value at position |index| in an attribute's |values|.
// If the attribute is too short, it is resized to (|index|+1) when possible.
// When |cut_if_longer| is set, the attribute is shrunk to (|index|+1) values if
// it is longer. If |cut_if_longer| equals false, the attribute is no
// downsized. The function never returns nullptr.
template <typename Type>
Type* ResizeAttrGetValuePtr(void*& values,
                            const AttrDef& def,
                            size_t index,
                            bool cut_if_longer) {
  // Create |values| if not exists.
  if (values == nullptr) {
    values = new std::vector<Type>();
  }
  // Returns the pointer, resize the attribute when needed.
  std::vector<Type>* v = reinterpret_cast<std::vector<Type>*>(values);
  if (cut_if_longer || v->size() <= index)
    ResizeVector<Type>(def, v, index + 1);
  return (v->data() + index);
}

// Resizes an attribute's |values| to |new_size| values. The parameter
// |cut_if_longer| works in the same way as in the previous template function.
void ResizeAttr(void*& values,
                const AttrDef& def,
                size_t new_size,
                bool cut_if_longer) {
  if (new_size == 0) {
    DeleteAttr(values, def);
    return;
  }
  switch (def.cc_type) {
    case InternalType::kInteger:
      ResizeAttrGetValuePtr<int32_t>(values, def, new_size - 1, cut_if_longer);
      break;
    case InternalType::kString:
      ResizeAttrGetValuePtr<std::string>(values, def, new_size - 1,
                                         cut_if_longer);
      break;
    case InternalType::kResolution:
      ResizeAttrGetValuePtr<Resolution>(values, def, new_size - 1,
                                        cut_if_longer);
      break;
    case InternalType::kRangeOfInteger:
      ResizeAttrGetValuePtr<RangeOfInteger>(values, def, new_size - 1,
                                            cut_if_longer);
      break;
    case InternalType::kDateTime:
      ResizeAttrGetValuePtr<DateTime>(values, def, new_size - 1, cut_if_longer);
      break;
    case InternalType::kStringWithLanguage:
      ResizeAttrGetValuePtr<StringWithLanguage>(values, def, new_size - 1,
                                                cut_if_longer);
      break;
    case InternalType::kCollection:
      ResizeAttrGetValuePtr<Collection*>(values, def, new_size - 1,
                                         cut_if_longer);
      break;
  }
}

// Reads a value at position |index| in an attribute's |values| and saves it
// to |value|. Proper conversion is applied when needed. The function returns
// true if succeeds and false when one of the following occurs:
// * |value| is nullptr
// * the attribute has less than |index|+1 values
// * required conversion is not possible
template <typename InternalType, typename ApiType>
bool ReadConvertValueTyped(void* const& values,
                           const std::string& name,
                           const AttrDef& def,
                           size_t index,
                           ApiType* value) {
  if (value == nullptr)
    return false;

  const InternalType* internal_value = nullptr;
  auto v = ReadValueConstPtr<std::vector<InternalType>>(&values);
  if (v->size() <= index)
    return false;
  internal_value = v->data() + index;
  return Converter<InternalType, ApiType>::Convert(name, def, *internal_value,
                                                   value);
}

// The same as previous one, just chooses correct template instantiation.
template <typename ApiType>
bool ReadConvertValue(void* const& values,
                      const std::string& name,
                      const AttrDef& def,
                      size_t index,
                      ApiType* value) {
  switch (def.cc_type) {
    case InternalType::kInteger:
      return ReadConvertValueTyped<int32_t, ApiType>(values, name, def, index,
                                                     value);
    case InternalType::kString:
      return ReadConvertValueTyped<std::string, ApiType>(values, name, def,
                                                         index, value);
    case InternalType::kResolution:
      return ReadConvertValueTyped<Resolution, ApiType>(values, name, def,
                                                        index, value);
    case InternalType::kRangeOfInteger:
      return ReadConvertValueTyped<RangeOfInteger, ApiType>(values, name, def,
                                                            index, value);
    case InternalType::kDateTime:
      return ReadConvertValueTyped<DateTime, ApiType>(values, name, def, index,
                                                      value);
    case InternalType::kStringWithLanguage:
      return ReadConvertValueTyped<StringWithLanguage, ApiType>(
          values, name, def, index, value);
    case InternalType::kCollection:
      return false;
  }
  return false;
}

// Saves |value| to position |index| in an attribute |name|,|def|. Proper
// conversion is applied when needed. The attribute is also resized when |index|
// is greater than the attribute's size. The function returns true if succeeds
// and false when the required conversion is not possible.
template <typename InternalType, typename ApiType>
bool SaveValueTyped(void*& values,
                    const std::string& name,
                    const AttrDef& def,
                    size_t index,
                    const ApiType& value) {
  InternalType internal_value;
  if (!Converter<ApiType, InternalType>::Convert(name, def, value,
                                                 &internal_value))
    return false;
  InternalType* internal_ptr =
      ResizeAttrGetValuePtr<InternalType>(values, def, index, false);
  *internal_ptr = internal_value;
  return true;
}

}  // end of namespace

std::string_view ToStrView(ValueTag tag) {
  switch (tag) {
    case ValueTag::unsupported:
      return std::string_view("unsupported");
    case ValueTag::unknown:
      return std::string_view("unknown");
    case ValueTag::no_value:
      return std::string_view("no-value");
    case ValueTag::not_settable:
      return std::string_view("not-settable");
    case ValueTag::delete_attribute:
      return std::string_view("delete-attribute");
    case ValueTag::admin_define:
      return std::string_view("admin-define");
    case ValueTag::integer:
      return std::string_view("integer");
    case ValueTag::boolean:
      return std::string_view("boolean");
    case ValueTag::enum_:
      return std::string_view("enum");
    case ValueTag::octetString:
      return std::string_view("octetString");
    case ValueTag::dateTime:
      return std::string_view("dateTime");
    case ValueTag::resolution:
      return std::string_view("resolution");
    case ValueTag::rangeOfInteger:
      return std::string_view("rangeOfInteger");
    case ValueTag::collection:
      return std::string_view("collection");
    case ValueTag::textWithLanguage:
      return std::string_view("textWithLanguage");
    case ValueTag::nameWithLanguage:
      return std::string_view("nameWithLanguage");
    case ValueTag::textWithoutLanguage:
      return std::string_view("textWithoutLanguage");
    case ValueTag::nameWithoutLanguage:
      return std::string_view("nameWithoutLanguage");
    case ValueTag::keyword:
      return std::string_view("keyword");
    case ValueTag::uri:
      return std::string_view("uri");
    case ValueTag::uriScheme:
      return std::string_view("uriScheme");
    case ValueTag::charset:
      return std::string_view("charset");
    case ValueTag::naturalLanguage:
      return std::string_view("naturalLanguage");
    case ValueTag::mimeMediaType:
      return std::string_view("mimeMediaType");
  }
  if (IsValid(tag)) {
    return std::string_view("<unknown_ValueTag>");
  }
  return std::string_view("<invalid_ValueTag>");
}

std::string ToString(bool v) {
  return (v ? "true" : "false");
}

std::string ToString(int v) {
  if (v < 0) {
    // 2 x incrementation in case of (v == numeric_limit<int>::min())
    const std::string s = UnsignedToString(static_cast<size_t>(-(++v)) + 1);
    return "-" + s;
  }
  return UnsignedToString(v);
}

std::string ToString(const Resolution& v) {
  std::string s = ToString(v.xres) + "x" + ToString(v.yres);
  if (v.units == Resolution::kDotsPerInch)
    s += "dpi";
  else
    s += "dpc";
  return s;
}

std::string ToString(const RangeOfInteger& v) {
  return ("(" + ToString(v.min_value) + ":" + ToString(v.max_value) + ")");
}

std::string ToString(const DateTime& v) {
  return (ToString(v.year) + "-" + ToString(v.month) + "-" + ToString(v.day) +
          "," + ToString(v.hour) + ":" + ToString(v.minutes) + ":" +
          ToString(v.seconds) + "." + ToString(v.deci_seconds) + "," +
          std::string(1, v.UTC_direction) + ToString(v.UTC_hours) + ":" +
          ToString(v.UTC_minutes));
}

std::string ToString(const StringWithLanguage& value) {
  return value.value;
}

bool FromString(const std::string& s, bool* v) {
  if (v == nullptr)
    return false;
  if (s == "false") {
    *v = false;
    return true;
  }
  if (s == "true") {
    *v = true;
    return true;
  }
  return false;
}

// JSON-like integer format: first character may be '-', the rest must be
// digits. Leading zeroes allowed.
bool FromString(const std::string& s, int* out) {
  if (out == nullptr)
    return false;
  if (s.empty())
    return false;
  auto it = s.begin();
  int vv = 0;
  if (*it == '-') {
    ++it;
    if (it == s.end())
      return false;
    // negative number
    for (; it != s.end(); ++it) {
      if (std::numeric_limits<int>::min() / 10 > vv)
        return false;
      vv *= 10;
      if (*it < '0' || *it > '9')
        return false;
      const int d = (*it - '0');
      if (std::numeric_limits<int>::min() + d > vv)
        return false;
      vv -= d;
    }
  } else {
    // positive number
    for (; it != s.end(); ++it) {
      if (std::numeric_limits<int>::max() / 10 < vv)
        return false;
      vv *= 10;
      if (*it < '0' || *it > '9')
        return false;
      const int d = (*it - '0');
      if (std::numeric_limits<int>::max() - d < vv)
        return false;
      vv += d;
    }
  }
  *out = vv;
  return true;
}

Attribute::~Attribute() {
  DeleteAttr(values_, def_);
}

ValueTag Attribute::Tag() const {
  return def_.ipp_type;
}

Attribute::Attribute(std::string_view name, AttrDef def)
    : name_(name), def_(def) {
  // Attributes with non-out-of-band tag must have at least one value.
  if (!IsOutOfBand(def.ipp_type)) {
    ResizeAttr(values_, def_, 1, false);
  }
}

std::string_view Attribute::Name() const {
  return name_;
}

size_t Attribute::GetSize() const {
  if (values_ == nullptr)
    return 0;
  switch (def_.cc_type) {
    case InternalType::kInteger:
      return ReadValueConstPtr<std::vector<int32_t>>(&values_)->size();
    case InternalType::kString:
      return ReadValueConstPtr<std::vector<std::string>>(&values_)->size();
    case InternalType::kResolution:
      return ReadValueConstPtr<std::vector<Resolution>>(&values_)->size();
    case InternalType::kRangeOfInteger:
      return ReadValueConstPtr<std::vector<RangeOfInteger>>(&values_)->size();
    case InternalType::kDateTime:
      return ReadValueConstPtr<std::vector<DateTime>>(&values_)->size();
    case InternalType::kStringWithLanguage:
      return ReadValueConstPtr<std::vector<StringWithLanguage>>(&values_)
          ->size();
    case InternalType::kCollection:
      return ReadValueConstPtr<std::vector<Collection*>>(&values_)->size();
  }
  return 0;
}

size_t Attribute::Size() const {
  return GetSize();
}

void Attribute::Resize(size_t new_size) {
  if (IsOutOfBand(def_.ipp_type) || new_size == 0)
    return;
  ResizeAttr(values_, def_, new_size, true);
}

bool Attribute::GetValue(std::string* val, size_t index) const {
  return ReadConvertValue(values_, name_, def_, index, val);
}

bool Attribute::GetValue(StringWithLanguage* val, size_t index) const {
  return ReadConvertValue(values_, name_, def_, index, val);
}

bool Attribute::GetValue(int* val, size_t index) const {
  return ReadConvertValue(values_, name_, def_, index, val);
}

bool Attribute::GetValue(Resolution* val, size_t index) const {
  return ReadConvertValue(values_, name_, def_, index, val);
}

bool Attribute::GetValue(RangeOfInteger* val, size_t index) const {
  return ReadConvertValue(values_, name_, def_, index, val);
}

bool Attribute::GetValue(DateTime* val, size_t index) const {
  return ReadConvertValue(values_, name_, def_, index, val);
}

bool Attribute::SetValue(const std::string& val, size_t index) {
  return SaveValue(index, val);
}

bool Attribute::SetValue(const StringWithLanguage& val, size_t index) {
  return SaveValue(index, val);
}

bool Attribute::SetValue(const int& val, size_t index) {
  return SaveValue(index, val);
}

bool Attribute::SetValue(const Resolution& val, size_t index) {
  return SaveValue(index, val);
}

bool Attribute::SetValue(const RangeOfInteger& val, size_t index) {
  return SaveValue(index, val);
}

bool Attribute::SetValue(const DateTime& val, size_t index) {
  return SaveValue(index, val);
}

Collection* Attribute::GetCollection(size_t index) {
  if (def_.cc_type != InternalType::kCollection)
    return nullptr;
  if (values_ == nullptr)
    return nullptr;
  Collection* p = nullptr;
  auto v = ReadValuePtr<std::vector<Collection*>>(&values_);
  if (v->size() > index)
    p = *(v->data() + index);
  return p;
}

const Collection* Attribute::GetCollection(size_t index) const {
  if (def_.cc_type != InternalType::kCollection)
    return nullptr;
  if (values_ == nullptr)
    return nullptr;
  const Collection* p = nullptr;
  auto v = ReadValueConstPtr<std::vector<Collection*>>(&values_);
  if (v->size() > index)
    p = *(v->data() + index);
  return p;
}

Collection::Collection() = default;

Collection::~Collection() = default;

Attribute* Collection::GetAttribute(AttrName an) {
  std::string_view name = ToStrView(an);
  auto it = attributes_index_.find(name);
  if (it != attributes_index_.end())
    return attributes_[it->second].get();
  return nullptr;
}

const Attribute* Collection::GetAttribute(AttrName an) const {
  std::string_view name = ToStrView(an);
  auto it = attributes_index_.find(name);
  if (it != attributes_index_.end())
    return attributes_[it->second].get();
  return nullptr;
}

Attribute* Collection::GetAttribute(const std::string& name) {
  auto it = attributes_index_.find(name);
  if (it != attributes_index_.end())
    return attributes_[it->second].get();
  return nullptr;
}

const Attribute* Collection::GetAttribute(const std::string& name) const {
  auto it = attributes_index_.find(name);
  if (it != attributes_index_.end())
    return attributes_[it->second].get();
  return nullptr;
}

Code Collection::CreateNewAttribute(const std::string& name,
                                    ValueTag type,
                                    Attribute*& new_attr) {
  // Check all constraints.
  if (name.empty() ||
      name.size() > static_cast<size_t>(std::numeric_limits<int16_t>::max())) {
    return Code::kInvalidName;
  }
  if (attributes_index_.count(name)) {
    return Code::kNameConflict;
  }
  if (!IsValid(type))
    return Code::kInvalidValueTag;
  // Create new attribute.
  AttrDef def;
  def.ipp_type = type;
  def.cc_type = InternalTypeForUnknownAttribute(type);
  new_attr = attributes_.emplace_back(new Attribute(name, def)).get();
  attributes_index_[new_attr->Name()] = attributes_.size() - 1;
  return Code::kOK;
}

template <typename ApiType>
Code Collection::AddAttributeToCollection(const std::string& name,
                                          ValueTag tag,
                                          const std::vector<ApiType>& values) {
  if (values.empty() && !IsOutOfBand(tag)) {
    return Code::kValueOutOfRange;
  }

  // Create a new attribute. For non-Out-Of-Band tags set the values.
  Attribute* attr = nullptr;
  if (Code result = CreateNewAttribute(name, tag, attr); result != Code::kOK) {
    return result;
  }
  if (!IsOutOfBand(tag)) {
    attr->Resize(values.size());
    for (size_t i = 0; i < values.size(); ++i)
      attr->SetValue(values[i], i);
  }

  return Code::kOK;
}

Code Collection::AddAttr(const std::string& name, ValueTag tag) {
  if (IsOutOfBand(tag)) {
    return AddAttributeToCollection(name, tag, std::vector<int32_t>());
  }
  return IsValid(tag) ? Code::kIncompatibleType : Code::kInvalidValueTag;
}

Code Collection::AddAttr(const std::string& name, ValueTag tag, int32_t value) {
  return AddAttr(name, tag, std::vector<int32_t>{value});
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         const std::string& value) {
  return AddAttr(name, tag, std::vector<std::string>{value});
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         const StringWithLanguage& value) {
  return AddAttr(name, tag, std::vector<StringWithLanguage>{value});
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         DateTime value) {
  return AddAttr(name, tag, std::vector<DateTime>{value});
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         Resolution value) {
  return AddAttr(name, tag, std::vector<Resolution>{value});
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         RangeOfInteger value) {
  return AddAttr(name, tag, std::vector<RangeOfInteger>{value});
}

Code Collection::AddAttr(const std::string& name, bool value) {
  return AddAttr(name, std::vector<bool>{value});
}

Code Collection::AddAttr(const std::string& name, int32_t value) {
  return AddAttr(name, std::vector<int32_t>{value});
}

Code Collection::AddAttr(const std::string& name, DateTime value) {
  return AddAttr(name, std::vector<DateTime>{value});
}

Code Collection::AddAttr(const std::string& name, Resolution value) {
  return AddAttr(name, std::vector<Resolution>{value});
}

Code Collection::AddAttr(const std::string& name, RangeOfInteger value) {
  return AddAttr(name, std::vector<RangeOfInteger>{value});
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         const std::vector<int32_t>& values) {
  switch (tag) {
    case ValueTag::integer:
      break;
    case ValueTag::enum_:  // see rfc8011-5.1.5
      for (int32_t v : values) {
        if (v < 1 || v > std::numeric_limits<int16_t>::max()) {
          return Code::kValueOutOfRange;
        }
      }
      break;
    case ValueTag::boolean:
      for (int32_t v : values) {
        if (v != 0 && v != 1) {
          return Code::kValueOutOfRange;
        }
      }
      break;
    default:
      return IsValid(tag) ? Code::kIncompatibleType : Code::kInvalidValueTag;
  }
  return AddAttributeToCollection(name, tag, values);
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         const std::vector<std::string>& values) {
  if (tag == ValueTag::octetString || IsString(tag)) {
    return AddAttributeToCollection(name, tag, values);
  }
  return IsValid(tag) ? Code::kIncompatibleType : Code::kInvalidValueTag;
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         const std::vector<StringWithLanguage>& values) {
  if (tag == ValueTag::nameWithLanguage || tag == ValueTag::textWithLanguage) {
    return AddAttributeToCollection(name, tag, values);
  }
  return IsValid(tag) ? Code::kIncompatibleType : Code::kInvalidValueTag;
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         const std::vector<DateTime>& values) {
  if (tag == ValueTag::dateTime) {
    return AddAttributeToCollection(name, tag, values);
  }
  return IsValid(tag) ? Code::kIncompatibleType : Code::kInvalidValueTag;
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         const std::vector<Resolution>& values) {
  if (tag == ValueTag::resolution) {
    return AddAttributeToCollection(name, tag, values);
  }
  return IsValid(tag) ? Code::kIncompatibleType : Code::kInvalidValueTag;
}

Code Collection::AddAttr(const std::string& name,
                         ValueTag tag,
                         const std::vector<RangeOfInteger>& values) {
  if (tag == ValueTag::rangeOfInteger) {
    return AddAttributeToCollection(name, tag, values);
  }
  return IsValid(tag) ? Code::kIncompatibleType : Code::kInvalidValueTag;
}

Code Collection::AddAttr(const std::string& name,
                         const std::vector<bool>& values) {
  return AddAttributeToCollection(name, ValueTag::boolean, values);
}

Code Collection::AddAttr(const std::string& name,
                         const std::vector<int32_t>& values) {
  return AddAttributeToCollection(name, ValueTag::integer, values);
}

Code Collection::AddAttr(const std::string& name,
                         const std::vector<DateTime>& values) {
  return AddAttributeToCollection(name, ValueTag::dateTime, values);
}

Code Collection::AddAttr(const std::string& name,
                         const std::vector<Resolution>& values) {
  return AddAttributeToCollection(name, ValueTag::resolution, values);
}

Code Collection::AddAttr(const std::string& name,
                         const std::vector<RangeOfInteger>& values) {
  return AddAttributeToCollection(name, ValueTag::rangeOfInteger, values);
}

Code Collection::AddAttr(const std::string& name, Collection*& value) {
  std::vector<Collection*> values(1);
  Code err = AddAttr(name, values);
  if (err == Code::kOK) {
    value = values.front();
  }
  return err;
}

Code Collection::AddAttr(const std::string& name,
                         std::vector<Collection*>& values) {
  if (values.empty()) {
    return Code::kValueOutOfRange;
  }
  // Create the attribute and retrieve the pointers.
  Attribute* attr = nullptr;
  if (Code result = CreateNewAttribute(name, ValueTag::collection, attr);
      result != Code::kOK) {
    return result;
  }
  attr->Resize(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    values[i] = attr->GetCollection(i);
  }

  return Code::kOK;
}

std::vector<Attribute*> Collection::GetAllAttributes() {
  std::vector<Attribute*> v;
  v.reserve(attributes_.size());
  for (std::unique_ptr<Attribute>& attr : attributes_)
    v.push_back(attr.get());
  return v;
}

std::vector<const Attribute*> Collection::GetAllAttributes() const {
  std::vector<const Attribute*> v;
  v.reserve(attributes_.size());
  for (const std::unique_ptr<Attribute>& attr : attributes_)
    v.push_back(attr.get());
  return v;
}

// Saves |value| at position |index|. Proper conversion is applied when needed.
// The attribute is also resized when |index| is greater than the attribute's
// size. The function returns true if succeeds and false when the required
// conversion is not possible (|value| is incorrect).
template <typename ApiType>
bool Attribute::SaveValue(size_t index, const ApiType& value) {
  if (IsOutOfBand(def_.ipp_type))
    return false;
  bool result = false;
  switch (def_.cc_type) {
    case InternalType::kInteger:
      result =
          SaveValueTyped<int32_t, ApiType>(values_, name_, def_, index, value);
      break;
    case InternalType::kString:
      result = SaveValueTyped<std::string, ApiType>(values_, name_, def_, index,
                                                    value);
      break;
    case InternalType::kResolution:
      result = SaveValueTyped<Resolution, ApiType>(values_, name_, def_, index,
                                                   value);
      break;
    case InternalType::kRangeOfInteger:
      result = SaveValueTyped<RangeOfInteger, ApiType>(values_, name_, def_,
                                                       index, value);
      break;
    case InternalType::kDateTime:
      result =
          SaveValueTyped<DateTime, ApiType>(values_, name_, def_, index, value);
      break;
    case InternalType::kStringWithLanguage:
      result = SaveValueTyped<StringWithLanguage, ApiType>(values_, name_, def_,
                                                           index, value);
      break;
    case InternalType::kCollection:
      return false;
  }
  return result;
}

}  // namespace ipp
