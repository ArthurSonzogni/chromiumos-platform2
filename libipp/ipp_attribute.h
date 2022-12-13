// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBIPP_IPP_ATTRIBUTE_H_
#define LIBIPP_IPP_ATTRIBUTE_H_

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ipp_enums.h"
#include "ipp_export.h"

namespace ipp {

// Forward declaration
enum class Code;

// Values of ValueTag enum are copied from IPP specification; this is why
// they do not follow the standard naming rule.
// ValueTag defines type of an attribute. It is also called as `syntax` in the
// IPP specification. All valid tags are listed below. Values of attributes with
// these tags are mapped to C++ types.
enum class ValueTag : uint8_t {
  // 0x00-0x0f are invalid.
  // 0x10-0x1f are Out-of-Band tags. Attributes with this tag have no values.
  // All tags from the range 0x10-0x1f are valid.
  unsupported = 0x10,       // [rfc8010]
  unknown = 0x12,           // [rfc8010]
  no_value = 0x13,          // [rfc8010]
  not_settable = 0x15,      // [rfc3380]
  delete_attribute = 0x16,  // [rfc3380]
  admin_define = 0x17,      // [rfc3380]
  // 0x20-0x2f represents integer types.
  // Only the following tags are valid. They map to int32_t.
  integer = 0x21,
  boolean = 0x22,  // maps to both int32_t and bool.
  enum_ = 0x23,
  // 0x30-0x3f are called "octetString types". They map to dedicated types.
  // Only the following tags are valid.
  octetString = 0x30,       // maps to std::string
  dateTime = 0x31,          // maps to DateTime
  resolution = 0x32,        // maps to Resolution
  rangeOfInteger = 0x33,    // maps to RangeOfInteger
  collection = 0x34,        // = begCollection tag [rfc8010], maps to Collection
  textWithLanguage = 0x35,  // maps to StringWithLanguage
  nameWithLanguage = 0x36,  // maps to StringWithLanguage
  // 0x40-0x5f represents 'character-string values'. They map to std::string.
  // All tags from the ranges 0x40-0x49 and 0x4b-0x5f are valid.
  textWithoutLanguage = 0x41,
  nameWithoutLanguage = 0x42,
  keyword = 0x44,
  uri = 0x45,
  uriScheme = 0x46,
  charset = 0x47,
  naturalLanguage = 0x48,
  mimeMediaType = 0x49
  // memberAttrName = 0x4a is invalid.
  // 0x60-0xff are invalid.
};

// Is valid Out-of-Band tag (0x10-0x1f).
constexpr bool IsOutOfBand(ValueTag tag) {
  return (tag >= static_cast<ValueTag>(0x10) &&
          tag <= static_cast<ValueTag>(0x1f));
}
// Is valid integer type (0x21-0x23).
constexpr bool IsInteger(ValueTag tag) {
  return (tag >= static_cast<ValueTag>(0x21) &&
          tag <= static_cast<ValueTag>(0x23));
}
// Is valid character-string type (0x40-0x5f without 0x4a).
constexpr bool IsString(ValueTag tag) {
  return (tag >= static_cast<ValueTag>(0x40) &&
          tag <= static_cast<ValueTag>(0x5f) &&
          tag != static_cast<ValueTag>(0x4a));
}
// Is valid tag.
constexpr bool IsValid(ValueTag tag) {
  return (IsOutOfBand(tag) || IsInteger(tag) || IsString(tag) ||
          (tag >= static_cast<ValueTag>(0x30) &&
           tag <= static_cast<ValueTag>(0x36)));
}

// It is used to hold name and text values (see [rfc8010]).
// If language == "" it represents nameWithoutLanguage or textWithoutLanguage,
// otherwise it represents nameWithLanguage or textWithLanguage.
struct StringWithLanguage {
  std::string value = "";
  std::string language = "";
  StringWithLanguage() = default;
  StringWithLanguage(const std::string& value, const std::string& language)
      : value(value), language(language) {}
  explicit StringWithLanguage(const std::string& value) : value(value) {}
  explicit StringWithLanguage(std::string&& value) : value(value) {}
  operator std::string() const { return value; }
};

// Represents resolution type from [rfc8010].
struct Resolution {
  int32_t xres = 0;
  int32_t yres = 0;
  enum Units : int8_t {
    kDotsPerInch = 3,
    kDotsPerCentimeter = 4
  } units = kDotsPerInch;
  Resolution() = default;
  Resolution(int32_t size1, int32_t size2, Units units = Units::kDotsPerInch)
      : xres(size1), yres(size2), units(units) {}
};

// Represents rangeOfInteger type from [rfc8010].
struct RangeOfInteger {
  int32_t min_value = 0;
  int32_t max_value = 0;
  RangeOfInteger() = default;
  RangeOfInteger(int32_t min_value, int32_t max_value)
      : min_value(min_value), max_value(max_value) {}
};

// Represents dateTime type from [rfc8010,rfc2579].
struct DateTime {
  uint16_t year = 2000;
  uint8_t month = 1;            // 1..12
  uint8_t day = 1;              // 1..31
  uint8_t hour = 0;             // 0..23
  uint8_t minutes = 0;          // 0..59
  uint8_t seconds = 0;          // 0..60 (60 - leap second :-)
  uint8_t deci_seconds = 0;     // 0..9
  uint8_t UTC_direction = '+';  // '+' / '-'
  uint8_t UTC_hours = 0;        // 0..13
  uint8_t UTC_minutes = 0;      // 0..59
};

// Functions converting basic types to string. For enums it returns empty
// string if given value is not defined.
IPP_EXPORT std::string_view ToStrView(ValueTag tag);
IPP_EXPORT std::string ToString(bool value);
IPP_EXPORT std::string ToString(int value);
IPP_EXPORT std::string ToString(const Resolution& value);
IPP_EXPORT std::string ToString(const RangeOfInteger& value);
IPP_EXPORT std::string ToString(const DateTime& value);
IPP_EXPORT std::string ToString(const StringWithLanguage& value);

// Functions extracting basic types from string.
// Returns false <=> given pointer is nullptr or given string does not
// represent a correct value.
IPP_EXPORT bool FromString(const std::string& str, bool* value);
IPP_EXPORT bool FromString(const std::string& str, int* value);

// Basic values are stored in attributes as variables of the following types:
enum InternalType : uint8_t {
  kInteger,             // int32_t
  kString,              // std::string
  kStringWithLanguage,  // ipp::StringWithLanguage
  kResolution,          // ipp::Resolution
  kRangeOfInteger,      // ipp::RangeOfInteger
  kDateTime,            // ipp::DateTime
  kCollection           // Collection*
};

class Attribute;
class Collection;

// Helper structure
struct AttrDef {
  ValueTag ipp_type;
  InternalType cc_type;
};

// Base class for all IPP collections. Collections is like struct filled with
// Attributes. Each attribute in Collection must have unique name.
class IPP_EXPORT Collection {
 public:
  Collection();
  Collection(const Collection&) = delete;
  Collection(Collection&&) = delete;
  Collection& operator=(const Collection&) = delete;
  Collection& operator=(Collection&&) = delete;
  virtual ~Collection();

  // Returns all attributes in the collection.
  // Returned vector = GetKnownAttributes() + unknown attributes.
  // Unknown attributes are in the order they were added to the collection.
  // There are no nullptrs in the returned vector.
  std::vector<Attribute*> GetAllAttributes();
  std::vector<const Attribute*> GetAllAttributes() const;

  // Methods return attribute by name. Methods return nullptr <=> the collection
  // has no attribute with this name.
  Attribute* GetAttribute(const std::string& name);
  const Attribute* GetAttribute(const std::string& name) const;

  // Adds new attribute to the collection. Returns nullptr <=> an attribute
  // with this name already exists in the collection or given name/type are
  // incorrect.
  Attribute* AddUnknownAttribute(const std::string& name, ValueTag type);

  // Add a new attribute without values. `tag` must be Out-Of-Band (see ValueTag
  // definition). Possible errors:
  //  * kInvalidName
  //  * kNameConflict
  //  * kInvalidValueTag
  //  * kIncompatibleType  (`tag` is not Out-Of-Band)
  //  * kTooManyAttributes.
  Code AddAttr(const std::string& name, ValueTag tag);

  // Add a new attribute with one or more values. `tag` must be compatible with
  // type of the parameter `value`/`values` according to the following rules:
  //  * int32_t: IsInteger(tag) == true
  //  * std::string: IsString(tag) == true OR tag == octetString
  //  * StringWithLanguage: tag == nameWithLanguage OR tag == textWithLanguage
  //  * DateTime: tag == dateTime
  //  * Resolution: tag == resolution
  //  * RangeOfInteger: tag == rangeOfInteger
  // Possible errors:
  //  * kInvalidName
  //  * kNameConflict
  //  * kInvalidValueTag
  //  * kIncompatibleType  (see the rules above)
  //  * kValueOutOfRange   (the vector is empty or one of the value is invalid)
  //  * kTooManyAttributes.
  Code AddAttr(const std::string& name, ValueTag tag, int32_t value);
  Code AddAttr(const std::string& name, ValueTag tag, const std::string& value);
  Code AddAttr(const std::string& name,
               ValueTag tag,
               const StringWithLanguage& value);
  Code AddAttr(const std::string& name, ValueTag tag, DateTime value);
  Code AddAttr(const std::string& name, ValueTag tag, Resolution value);
  Code AddAttr(const std::string& name, ValueTag tag, RangeOfInteger value);
  Code AddAttr(const std::string& name,
               ValueTag tag,
               const std::vector<int32_t>& values);
  Code AddAttr(const std::string& name,
               ValueTag tag,
               const std::vector<std::string>& values);
  Code AddAttr(const std::string& name,
               ValueTag tag,
               const std::vector<StringWithLanguage>& values);
  Code AddAttr(const std::string& name,
               ValueTag tag,
               const std::vector<DateTime>& values);
  Code AddAttr(const std::string& name,
               ValueTag tag,
               const std::vector<Resolution>& values);
  Code AddAttr(const std::string& name,
               ValueTag tag,
               const std::vector<RangeOfInteger>& values);

  // Add a new attribute with one or more values. Tag of the new attribute is
  // deduced from the type of the parameter `value`/`values`.
  // Possible errors:
  //  * kInvalidName
  //  * kNameConflict
  //  * kValueOutOfRange   (the vector is empty)
  //  * kTooManyAttributes.
  Code AddAttr(const std::string& name, bool value);
  Code AddAttr(const std::string& name, int32_t value);
  Code AddAttr(const std::string& name, DateTime value);
  Code AddAttr(const std::string& name, Resolution value);
  Code AddAttr(const std::string& name, RangeOfInteger value);
  Code AddAttr(const std::string& name, const std::vector<bool>& values);
  Code AddAttr(const std::string& name, const std::vector<int32_t>& values);
  Code AddAttr(const std::string& name, const std::vector<DateTime>& values);
  Code AddAttr(const std::string& name, const std::vector<Resolution>& values);
  Code AddAttr(const std::string& name,
               const std::vector<RangeOfInteger>& values);

  // Add a new attribute with one or more collections. Pointers to created
  // collections are returned in the last parameter. The size of the vector
  // `values` determines the number of collections in the attribute.
  // Possible errors:
  //  * kInvalidName
  //  * kNameConflict
  //  * kValueOutOfRange   (the vector is empty)
  //  * kTooManyAttributes.
  Code AddAttr(const std::string& name, Collection*& value);
  Code AddAttr(const std::string& name, std::vector<Collection*>& values);

 private:
  friend class Attribute;

  // Methods return attribute by name. Methods return nullptr <=> the collection
  // has no attribute with this name.
  Attribute* GetAttribute(AttrName);
  const Attribute* GetAttribute(AttrName) const;

  // Mapping between temporary AttrName created for unknown attributes and
  // their real names.
  std::map<AttrName, std::string> unknown_names;

  // Stores attributes in the order they are saved in the frame.
  std::vector<std::unique_ptr<Attribute>> attributes_;

  // Indexes attributes by name. Values are indices from `attributes_`.
  std::unordered_map<std::string_view, size_t> attributes_index_;
};

// Base class representing Attribute, contains general API for Attribute.
class IPP_EXPORT Attribute {
 public:
  Attribute(const Attribute&) = delete;
  Attribute(Attribute&&) = delete;
  Attribute& operator=(const Attribute&) = delete;
  Attribute& operator=(Attribute&&) = delete;

  virtual ~Attribute();

  // Returns tag of the attribute.
  ValueTag Tag() const;

  // Returns an attribute's name. It is always a non-empty string.
  std::string_view Name() const;

  // Returns the current number of elements (values or Collections).
  // It returns 0 <=> IsOutOfBand(Tag()).
  size_t Size() const;

  // Resizes the attribute (changes the number of stored values/collections).
  // When (IsOutOfBand(Tag()) or `new_size` equals 0 this method does nothing.
  void Resize(size_t new_size);

  // Retrieves a value from an attribute, returns true for success and
  // false if the index is out of range or the value cannot be converted
  // to given variable (in this case, the given variable is not modified).
  // For attributes with collections use GetCollection().
  // (val == nullptr) => does nothing and returns false.
  // (Tag() == collection) => does nothing and returns false.
  bool GetValue(std::string* val, size_t index = 0) const;
  bool GetValue(StringWithLanguage* val, size_t index = 0) const;
  bool GetValue(int* val, size_t index = 0) const;
  bool GetValue(Resolution* val, size_t index = 0) const;
  bool GetValue(RangeOfInteger* val, size_t index = 0) const;
  bool GetValue(DateTime* val, size_t index = 0) const;

  // Stores a value in given attribute's element. If given index is out of
  // range, the underlying container is resized.
  // Returns true for success and false if given value cannot be converted
  // to internal variable or one of the following conditions are met:
  // * Tag() == collection
  // * IsOutOfBand(Tag())
  bool SetValue(const std::string& val, size_t index = 0);
  bool SetValue(const StringWithLanguage& val, size_t index = 0);
  bool SetValue(const int& val, size_t index = 0);
  bool SetValue(const Resolution& val, size_t index = 0);
  bool SetValue(const RangeOfInteger& val, size_t index = 0);
  bool SetValue(const DateTime& val, size_t index = 0);

  // Returns a pointer to Collection.
  // (Tag() != collection || index >= Size()) <=> returns nullptr.
  Collection* GetCollection(size_t index = 0);
  const Collection* GetCollection(size_t index = 0) const;

 private:
  friend class Collection;

  // Constructor is called from Collection only. `owner` cannot be nullptr.
  Attribute(Collection* owner, AttrName name, AttrDef def);

  // Returns enum value corresponding to attributes name. If the name has
  // no corresponding AttrName value, it returns AttrName::_unknown.
  AttrName GetNameAsEnum() const {
    if (ToString(name_) == "")
      return AttrName::_unknown;
    return name_;
  }

  // Returns the current number of elements (values or Collections).
  // (IsASet() == false) => always returns 0 or 1.
  size_t GetSize() const;

  // Helper template function.
  template <typename ApiType>
  bool SaveValue(size_t index, const ApiType& value);

  Collection* const owner_;
  const AttrName name_;

  // Defines the type of values stored in the attribute.
  const AttrDef def_;

  // Stores values of the attribute.
  void* values_ = nullptr;
};

}  // namespace ipp

#endif  //  LIBIPP_IPP_ATTRIBUTE_H_
