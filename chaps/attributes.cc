// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/attributes.h"

#include <memory>
#include <string>
#include <vector>

#include <base/logging.h>

#include "chaps/chaps_utility.h"
#include "chaps/proto_bindings/attributes.pb.h"

using std::string;
using std::vector;

namespace chaps {

Attributes::Attributes()
    : attributes_(NULL), num_attributes_(0), is_free_required_(false) {}

Attributes::Attributes(CK_ATTRIBUTE_PTR attributes, CK_ULONG num_attributes)
    : attributes_(attributes),
      num_attributes_(num_attributes),
      is_free_required_(false) {}

Attributes::~Attributes() {
  Free();
}

bool Attributes::Serialize(vector<uint8_t>* serialized_attributes) const {
  string tmp;
  if (!SerializeInternal(attributes_, num_attributes_,
                         true,  // Allow nesting.
                         &tmp))
    return false;
  *serialized_attributes = ConvertByteStringToVector(tmp);
  return true;
}

bool Attributes::Parse(const vector<uint8_t>& serialized_attributes) {
  Free();
  bool success = ParseInternal(ConvertByteVectorToString(serialized_attributes),
                               true,  // Allow nesting.
                               &attributes_, &num_attributes_);
  is_free_required_ = success;
  return success;
}

bool Attributes::ParseAndFill(const vector<uint8_t>& serialized_attributes) {
  return ParseAndFillInternal(ConvertByteVectorToString(serialized_attributes),
                              true,  // Allow nesting.
                              attributes_, num_attributes_);
}

bool Attributes::IsAttributeNested(CK_ATTRIBUTE_TYPE type) {
  return (type == CKA_WRAP_TEMPLATE || type == CKA_UNWRAP_TEMPLATE);
}

void Attributes::FreeAttributes(CK_ATTRIBUTE_PTR attributes,
                                CK_ULONG num_attributes) {
  FreeAttributesInternal(attributes, num_attributes,
                         true);  // Allow nesting.
}

void Attributes::Free() {
  if (is_free_required_) {
    FreeAttributes(attributes_, num_attributes_);
    attributes_ = NULL;
    num_attributes_ = 0;
  }
}

bool Attributes::SerializeInternal(CK_ATTRIBUTE_PTR attributes,
                                   CK_ULONG num_attributes,
                                   bool is_nesting_allowed,
                                   string* serialized) const {
  // The PKCS #11 specification explicitly defines this as -1 cast to CK_ULONG.
  // See the C_GetAttributeValue section, page 133 in v2.20.
  const CK_ULONG kErrorIndicator = static_cast<CK_ULONG>(-1);
  AttributeList attribute_list;
  for (CK_ULONG i = 0; i < num_attributes; ++i) {
    bool is_attribute_nested = IsAttributeNested(attributes[i].type);
    if (is_attribute_nested && !is_nesting_allowed) {
      LOG(ERROR) << "Nesting attempted and not allowed.";
      return false;
    }
    Attribute* next = attribute_list.add_attribute();
    next->set_type(attributes[i].type);
    next->set_length(attributes[i].ulValueLen);
    if (!attributes[i].pValue || attributes[i].ulValueLen == kErrorIndicator) {
      // The caller is to receive length only so we won't put a value in the
      // proto-buffer.
      continue;
    }
    if (!is_attribute_nested) {
      next->set_value(AttributeValueToString(attributes[i]));
      continue;
    }
    // When the attribute itself is an array of attributes, we need to
    // recurse.  Recursion is only allowed once because the PKCS #11
    // specification has no cases that require more and we don't want
    // malicious attributes to cause stack overflow.
    CK_ATTRIBUTE_PTR inner_attributes =
        reinterpret_cast<CK_ATTRIBUTE_PTR>(attributes[i].pValue);
    CK_ULONG num_inner_attributes =
        attributes[i].ulValueLen / sizeof(CK_ATTRIBUTE);
    string inner_serialized;
    if (!SerializeInternal(inner_attributes, num_inner_attributes,
                           false,  // Do not allow nesting.
                           &inner_serialized))
      return false;
    next->set_value(inner_serialized);
  }
  return attribute_list.SerializeToString(serialized);
}

bool Attributes::ParseInternal(const string& serialized,
                               bool is_nesting_allowed,
                               CK_ATTRIBUTE_PTR* attributes,
                               CK_ULONG* num_attributes) {
  AttributeList attribute_list;
  if (!attribute_list.ParseFromString(serialized)) {
    LOG(ERROR) << "Failed to parse proto-buffer.";
    return false;
  }
  std::unique_ptr<CK_ATTRIBUTE[]> attribute_array(
      new CK_ATTRIBUTE[attribute_list.attribute_size()]);
  CHECK(attribute_array.get());
  for (int i = 0; i < attribute_list.attribute_size(); ++i) {
    const Attribute& attribute = attribute_list.attribute(i);
    attribute_array[i].type = attribute.type();
    if (!attribute.has_value()) {
      // Only a length was requested, this is indicated in a CK_ATTRIBUTE by a
      // NULL pValue.
      attribute_array[i].ulValueLen = IntToValueLength(attribute.length());
      attribute_array[i].pValue = NULL;
      continue;
    }
    if (!IsAttributeNested(attribute_array[i].type)) {
      attribute_array[i].ulValueLen = attribute.value().length();
      attribute_array[i].pValue = new CK_BYTE[attribute.value().length()];
      CHECK(attribute_array[i].pValue);
      memcpy(attribute_array[i].pValue, attribute.value().data(),
             attribute.value().length());
      continue;
    }
    if (!is_nesting_allowed) {
      LOG(ERROR) << "Nesting attempted and not allowed.";
      return false;
    }
    // The value is a nested attribute list and needs to be parsed.
    CK_ATTRIBUTE_PTR inner_attribute_list = NULL;
    CK_ULONG num_inner_attributes = 0;
    if (!ParseInternal(attribute.value(),
                       false,  // Do not allow nesting.
                       &inner_attribute_list, &num_inner_attributes)) {
      LOG(ERROR) << "Nested parse failed.";
      return false;
    }
    attribute_array[i].ulValueLen = num_inner_attributes * sizeof(CK_ATTRIBUTE);
    attribute_array[i].pValue = inner_attribute_list;
  }
  *attributes = attribute_array.release();
  *num_attributes = attribute_list.attribute_size();
  return true;
}

bool Attributes::ParseAndFillInternal(const string& serialized,
                                      bool is_nesting_allowed,
                                      CK_ATTRIBUTE_PTR attributes,
                                      CK_ULONG num_attributes) {
  AttributeList attribute_list;
  if (!attributes) {
    LOG(ERROR) << "Attempted to fill NULL attribute array.";
    return false;
  }
  if (!attribute_list.ParseFromString(serialized)) {
    LOG(ERROR) << "Failed to parse proto-buffer.";
    return false;
  }
  if (num_attributes != IntToValueLength(attribute_list.attribute_size())) {
    LOG(ERROR) << "Attribute array size mismatch (expected=" << num_attributes
               << ", actual=" << attribute_list.attribute_size() << ").";
    return false;
  }
  for (int i = 0; i < attribute_list.attribute_size(); ++i) {
    const Attribute& attribute = attribute_list.attribute(i);
    if (attributes[i].type != attribute.type()) {
      LOG(ERROR) << "Attribute type mismatch (expected=" << attributes[i].type
                 << ", actual=" << attribute.type() << ").";
      return false;
    }
    if (!attribute.has_value()) {
      // Only a length is provided.  A NULL pValue is fine.
      attributes[i].ulValueLen = IntToValueLength(attribute.length());
      continue;
    }
    if (!attributes[i].pValue) {
      LOG(ERROR) << "Attempted to fill NULL attribute.";
      return false;
    }
    if (!IsAttributeNested(attributes[i].type)) {
      if (attribute.value().length() > attributes[i].ulValueLen) {
        LOG(ERROR) << "Attribute value overflow (length="
                   << attribute.value().length()
                   << ", max=" << attributes[i].ulValueLen << ").";
        return false;
      }
      attributes[i].ulValueLen = attribute.value().length();
      memcpy(attributes[i].pValue, attribute.value().data(),
             attribute.value().length());
      continue;
    }
    if (!is_nesting_allowed) {
      LOG(ERROR) << "Nesting attempted and not allowed.";
      return false;
    }
    // The value is a nested attribute list and needs to be parsed.
    CK_ATTRIBUTE_PTR inner_attribute_list =
        reinterpret_cast<CK_ATTRIBUTE_PTR>(attributes[i].pValue);
    CK_ULONG num_inner_attributes =
        attributes[i].ulValueLen / sizeof(CK_ATTRIBUTE);
    if (!ParseAndFillInternal(attribute.value(),
                              false,  // Do not allow nesting.
                              inner_attribute_list, num_inner_attributes)) {
      LOG(ERROR) << "Nested parse failed.";
      return false;
    }
  }
  return true;
}

void Attributes::FreeAttributesInternal(CK_ATTRIBUTE_PTR attributes,
                                        CK_ULONG num_attributes,
                                        bool is_nesting_allowed) {
  DCHECK(attributes != NULL);
  for (CK_ULONG i = 0; i < num_attributes; ++i) {
    if (!attributes[i].pValue)
      continue;
    if (!IsAttributeNested(attributes[i].type)) {
      // This was allocated as a CK_BYTE array, delete the same way.
      CK_BYTE_PTR value = reinterpret_cast<CK_BYTE_PTR>(attributes[i].pValue);
      delete[] value;
      continue;
    }
    // If nesting is not allowed, then the array is malformed and we won't
    // process it recursively.
    DCHECK(is_nesting_allowed);
    if (!is_nesting_allowed)
      continue;
    // This attribute is itself an attribute array; we need to recurse.
    CK_ATTRIBUTE_PTR inner_attribute_list =
        reinterpret_cast<CK_ATTRIBUTE_PTR>(attributes[i].pValue);
    CK_ULONG num_inner_attributes =
        attributes[i].ulValueLen / sizeof(CK_ATTRIBUTE);
    FreeAttributesInternal(inner_attribute_list, num_inner_attributes,
                           false);  // Do not allow nesting.
  }
  delete[] attributes;
}

// Convert int to CK_ULONG preserving -1.  Unfortunately, PKCS #11 uses -1 as a
// special value for the length field in CK_ATTRIBUTE.
CK_ULONG Attributes::IntToValueLength(int i) {
  if (i == -1)
    return ~0UL;
  return static_cast<CK_ULONG>(i);
}

string Attributes::AttributeValueToString(const CK_ATTRIBUTE& attributes) {
  return string(reinterpret_cast<char*>(attributes.pValue),
                attributes.ulValueLen);
}

}  // namespace chaps
