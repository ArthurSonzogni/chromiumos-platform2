// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_ATTRIBUTES_H
#define CHAPS_ATTRIBUTES_H

#include <string>

#include <base/basictypes.h>

#include "pkcs11/cryptoki.h"

namespace chaps {

// This class encapsulates an array of CK_ATTRIBUTEs and provides serialization.
class Attributes {
 public:
  // This constructor initializes with a NULL array.
  Attributes();
  // This constructor does not take ownership of the array.  I.e. No memory
  // deallocation will be performed when the object destructs.
  Attributes(CK_ATTRIBUTE_PTR attributes, CK_ULONG num_attributes);
  virtual ~Attributes();
  CK_ATTRIBUTE_PTR attributes() const {return attributes_;}
  CK_ULONG num_attributes() const {return num_attributes_;}
  // This method serializes the current array of attributes.
  virtual bool Serialize(std::string* serialized_attributes) const;
  // This method parses a serialized array of attributes into a new CK_ATTRIBUTE
  // array.  Any previous array will be deleted if necessary and discarded.
  virtual bool Parse(const std::string& serialized_attributes);
  // This method parses a serialized array of attributes and fills the current
  // attribute array with the values.  No memory will be allocated.  The number
  // and type of attributes parsed must match exactly the number and type of
  // attributes in the current array.  Also, the current array must have all
  // necessary memory allocated to receive parsed values.
  virtual bool ParseAndFill(const std::string& serialized_attributes);

  // This method determines if a given attribute holds a nested attribute array.
  static bool IsAttributeNested(CK_ATTRIBUTE_TYPE type);
  // This method recursively deallocates an array of attributes.  Each value
  // will be deallocated as well as the array itself.  Nested attribute arrays
  // will only be deallocated to a single level.
  static void FreeAttributes(CK_ATTRIBUTE_PTR attributes,
                             CK_ULONG num_attributes);

 private:
  void Free();
  bool SerializeInternal(CK_ATTRIBUTE_PTR attributes,
                         CK_ULONG num_attributes,
                         bool is_nesting_allowed,
                         std::string* serialized_attributes) const;
  bool ParseInternal(const std::string& serialized_attributes,
                     bool is_nesting_allowed,
                     CK_ATTRIBUTE_PTR* attributes,
                     CK_ULONG* num_attributes);
  bool ParseAndFillInternal(const std::string& serialized,
                            bool is_nesting_allowed,
                            CK_ATTRIBUTE_PTR attributes,
                            CK_ULONG num_attributes);

  static void FreeAttributesInternal(CK_ATTRIBUTE_PTR attributes,
                                     CK_ULONG num_attributes,
                                     bool is_nesting_allowed);
  static CK_ULONG IntToValueLength(int i);
  static std::string AttributeValueToString(const CK_ATTRIBUTE& attributes);

  // The array being managed (i.e. the 'current' array).
  CK_ATTRIBUTE_PTR attributes_;
  CK_ULONG num_attributes_;
  // This tracks whether attributes_ was allocated internally and needs to be
  // deallocated.
  bool is_free_required_;

  DISALLOW_COPY_AND_ASSIGN(Attributes);
};

}  // namespace

#endif  // CHAPS_ATTRIBUTES_H
