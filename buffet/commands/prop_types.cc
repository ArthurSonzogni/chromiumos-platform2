// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/commands/prop_types.h"

#include <algorithm>
#include <limits>
#include <set>

#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/values.h>
#include <chromeos/any.h>
#include <chromeos/strings/string_utils.h>

#include "buffet/commands/object_schema.h"
#include "buffet/commands/prop_values.h"
#include "buffet/commands/schema_constants.h"

namespace buffet {

// PropType -------------------------------------------------------------------
PropType::PropType() {
}

PropType::~PropType() {
}

std::string PropType::GetTypeAsString() const {
  return GetTypeStringFromType(GetType());
}

bool PropType::HasOverriddenAttributes() const {
  if (default_.value && !default_.is_inherited)
    return true;

  for (const auto& pair : constraints_) {
    if (pair.second->HasOverriddenAttributes())
      return true;
  }
  return false;
}

std::unique_ptr<base::Value> PropType::ToJson(bool full_schema,
                                              chromeos::ErrorPtr* error) const {
  if (!full_schema && !HasOverriddenAttributes()) {
    if (based_on_schema_)
      return std::unique_ptr<base::Value>(new base::DictionaryValue);
    return TypedValueToJson(GetTypeAsString(), error);
  }

  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue);
  if (full_schema) {
    // If we are asked for full_schema definition, then we need to output every
    // parameter property, including the "type", and any constraints.
    // So, we write the "type" only if asked for full schema.
    // Otherwise we will be able to infer the parameter type based on
    // the constraints and their types.
    // That is, the following JSONs could possibly define a parameter:
    // {'type':'integer'} -> explicit "integer" with no constraints
    // {'minimum':10} -> no type specified, but since we have "minimum"
    //                   and 10 is integer, than this is an integer
    //                   parameter with min constraint.
    // {'enum':[1,2,3]} -> integer with OneOf constraint.
    // And so is this: [1,2,3] -> an array of ints assume it's an "int" enum.
    dict->SetString(commands::attributes::kType, GetTypeAsString());
  }

  if (!full_schema && constraints_.size() == 1) {
    // If we are not asked for full schema, and we have only one constraint
    // which is OneOf, we short-circuit the whole thing and return just
    // the array [1,2,3] instead of an object with "enum" property like:
    // {'enum':[1,2,3]}
    auto p = constraints_.find(ConstraintType::OneOf);
    if (p != constraints_.end()) {
      return p->second->ToJson(error);
    }
  }

  for (const auto& pair : constraints_) {
    if (!pair.second->AddToJsonDict(dict.get(), !full_schema, error))
      return std::unique_ptr<base::Value>();
  }

  if (default_.value && (full_schema || !default_.is_inherited)) {
    auto defval = default_.value->ToJson(error);
    if (!defval)
      return std::unique_ptr<base::Value>();
    dict->Set(commands::attributes::kDefault, defval.release());
  }

  return std::unique_ptr<base::Value>(dict.release());
}

std::unique_ptr<PropType> PropType::Clone() const {
  auto cloned = PropType::Create(GetType());
  cloned->based_on_schema_ = based_on_schema_;
  for (const auto& pair : constraints_) {
    cloned->constraints_.emplace(pair.first, pair.second->Clone());
  }
  cloned->default_.is_inherited = default_.is_inherited;
  if (default_.value)
    cloned->default_.value = default_.value->Clone();
  return cloned;
}

bool PropType::FromJson(const base::DictionaryValue* value,
                        const PropType* base_schema,
                        chromeos::ErrorPtr* error) {
  if (base_schema && base_schema->GetType() != GetType()) {
    chromeos::Error::AddToPrintf(error, FROM_HERE, errors::commands::kDomain,
                                 errors::commands::kPropTypeChanged,
                                 "Redefining a property of type %s as %s",
                                 base_schema->GetTypeAsString().c_str(),
                                 GetTypeAsString().c_str());
    return false;
  }
  based_on_schema_ = (base_schema != nullptr);
  constraints_.clear();
  // Add the well-known object properties first (like "type", "displayName",
  // "default") to the list of "processed" keys so we do not complain about them
  // when we check for unknown/unexpected keys below.
  std::set<std::string> processed_keys{
      commands::attributes::kType,
      commands::attributes::kDisplayName,
      commands::attributes::kDefault,
  };
  if (!ObjectSchemaFromJson(value, base_schema, &processed_keys, error))
    return false;
  if (base_schema) {
    for (const auto& pair : base_schema->GetConstraints()) {
      constraints_.emplace(pair.first, pair.second->CloneAsInherited());
    }
  }
  if (!ConstraintsFromJson(value, &processed_keys, error))
    return false;

  // Now make sure there are no unexpected/unknown keys in the property schema
  // definition object.
  base::DictionaryValue::Iterator iter(*value);
  while (!iter.IsAtEnd()) {
    std::string key = iter.key();
    if (processed_keys.find(key) == processed_keys.end()) {
      chromeos::Error::AddToPrintf(error, FROM_HERE, errors::commands::kDomain,
                                   errors::commands::kUnknownProperty,
                                   "Unexpected property '%s'", key.c_str());
      return false;
    }
    iter.Advance();
  }

  // Read the default value, if specified.
  // We need to do this last since the current type definition must be complete,
  // so we can parse and validate the value of the default.
  const base::Value* defval = nullptr;  // Owned by value
  if (value->GetWithoutPathExpansion(commands::attributes::kDefault, &defval)) {
    std::unique_ptr<PropValue> prop_value = CreateValue();
    if (!prop_value->FromJson(defval, error) ||
        !ValidateValue(prop_value->GetValueAsAny(), error)) {
      chromeos::Error::AddToPrintf(error, FROM_HERE, errors::commands::kDomain,
                                   errors::commands::kInvalidPropValue,
                                   "Invalid value for property '%s'",
                                   commands::attributes::kDefault);
      return false;
    }
    default_.value = std::move(prop_value);
    default_.is_inherited = false;
  } else if (base_schema) {
    // If we have the base schema, inherit the type's default value from it.
    // It doesn't matter if the base schema actually has a default value
    // specified or not. If it doesn't, then the current type definition will
    // have no default value set either (|default_.value| is a unique_ptr to
    // PropValue, which can be set to nullptr).
    if (base_schema->default_.value)
      default_.value = base_schema->default_.value->Clone();
    default_.is_inherited = true;
  }
  return true;
}

void PropType::AddConstraint(std::unique_ptr<Constraint> constraint) {
  constraints_[constraint->GetType()] = std::move(constraint);
}

void PropType::RemoveConstraint(ConstraintType constraint_type) {
  constraints_.erase(constraint_type);
}

void PropType::RemoveAllConstraints() {
  constraints_.clear();
}

const Constraint* PropType::GetConstraint(
    ConstraintType constraint_type) const {
  auto p = constraints_.find(constraint_type);
  return p != constraints_.end() ? p->second.get() : nullptr;
}

Constraint* PropType::GetConstraint(ConstraintType constraint_type) {
  auto p = constraints_.find(constraint_type);
  return p != constraints_.end() ? p->second.get() : nullptr;
}

bool PropType::ValidateValue(const base::Value* value,
                             chromeos::ErrorPtr* error) const {
  std::unique_ptr<PropValue> val = CreateValue();
  CHECK(val) << "Failed to create value object";
  return val->FromJson(value, error) && ValidateConstraints(*val, error);
}

bool PropType::ValidateValue(const chromeos::Any& value,
                             chromeos::ErrorPtr* error) const {
  std::unique_ptr<PropValue> val = CreateValue(value, error);
  return val && ValidateConstraints(*val, error);
}

bool PropType::ValidateConstraints(const PropValue& value,
                                   chromeos::ErrorPtr* error) const {
  for (const auto& pair : constraints_) {
    if (!pair.second->Validate(value, error))
      return false;
  }
  return true;
}

const PropType::TypeMap& PropType::GetTypeMap() {
  static TypeMap map = {
    {ValueType::Int,         "integer"},
    {ValueType::Double,      "number"},
    {ValueType::String,      "string"},
    {ValueType::Boolean,     "boolean"},
    {ValueType::Object,      "object"},
  };
  return map;
}

std::string PropType::GetTypeStringFromType(ValueType type) {
  for (const auto& pair : GetTypeMap()) {
    if (pair.first == type)
      return pair.second;
  }
  LOG(FATAL) << "Type map is missing a type";
  return std::string();
}

bool PropType::GetTypeFromTypeString(const std::string& name, ValueType* type) {
  for (const auto& pair : GetTypeMap()) {
    if (pair.second == name) {
      *type = pair.first;
      return true;
    }
  }
  return false;
}

std::unique_ptr<PropType> PropType::Create(ValueType type) {
  PropType* prop = nullptr;
  switch (type) {
  case buffet::ValueType::Int:
    prop = new IntPropType;
    break;
  case buffet::ValueType::Double:
    prop = new DoublePropType;
    break;
  case buffet::ValueType::String:
    prop = new StringPropType;
    break;
  case buffet::ValueType::Boolean:
    prop = new BooleanPropType;
    break;
  case buffet::ValueType::Object:
    prop = new ObjectPropType;
    break;
  }
  return std::unique_ptr<PropType>(prop);
}

bool PropType::GenerateErrorValueTypeMismatch(chromeos::ErrorPtr* error) const {
  chromeos::Error::AddToPrintf(error, FROM_HERE, errors::commands::kDomain,
                                errors::commands::kTypeMismatch,
                                "Unable to convert value to type '%s'",
                                GetTypeAsString().c_str());
  return false;
}

template<typename T>
static std::unique_ptr<Constraint> LoadOneOfConstraint(
    const base::DictionaryValue* value,
    const PropType* prop_type,
    chromeos::ErrorPtr* error) {
  std::unique_ptr<Constraint> constraint;
  const base::ListValue* list = nullptr;
  if (!value->GetListWithoutPathExpansion(commands::attributes::kOneOf_Enum,
                                          &list)) {
    chromeos::Error::AddTo(error, FROM_HERE, errors::commands::kDomain,
                           errors::commands::kTypeMismatch,
                           "Expecting an array");
    return constraint;
  }
  std::vector<T> set;
  set.reserve(list->GetSize());
  for (const base::Value* item : *list) {
    T val{};
    if (!TypedValueFromJson(item, prop_type, &val, error))
      return constraint;
    set.push_back(val);
  }
  InheritableAttribute<std::vector<T>> val(set, false);
  constraint.reset(new ConstraintOneOf<T>{val});
  return constraint;
}

template<class ConstraintClass, typename T>
static std::unique_ptr<Constraint> LoadMinMaxConstraint(
    const char* dict_key,
    const base::DictionaryValue* value,
    chromeos::ErrorPtr* error) {
  std::unique_ptr<Constraint> constraint;
  InheritableAttribute<T> limit;

  const base::Value* src_val = nullptr;
  CHECK(value->Get(dict_key, &src_val)) << "Unable to get min/max constraints";
  if (!TypedValueFromJson(src_val, nullptr, &limit.value, error))
    return constraint;
  limit.is_inherited = false;

  constraint.reset(new ConstraintClass{limit});
  return constraint;
}

// PropTypeBase ----------------------------------------------------------------

template<class Derived, class Value, typename T>
bool PropTypeBase<Derived, Value, T>::ConstraintsFromJson(
    const base::DictionaryValue* value,
    std::set<std::string>* processed_keys,
    chromeos::ErrorPtr* error) {
  if (!PropType::ConstraintsFromJson(value, processed_keys, error))
    return false;

  if (value->HasKey(commands::attributes::kOneOf_Enum)) {
    auto type = Clone();
    type->RemoveAllConstraints();
    auto constraint = LoadOneOfConstraint<T>(value, type.get(), error);
    if (!constraint)
      return false;
    this->AddConstraint(std::move(constraint));
    this->RemoveConstraint(ConstraintType::Min);
    this->RemoveConstraint(ConstraintType::Max);
    processed_keys->insert(commands::attributes::kOneOf_Enum);
  }

  return true;
}

// NumericPropTypeBase ---------------------------------------------------------

template<class Derived, class Value, typename T>
bool NumericPropTypeBase<Derived, Value, T>::ConstraintsFromJson(
    const base::DictionaryValue* value,
    std::set<std::string>* processed_keys,
    chromeos::ErrorPtr* error) {
  if (!_Base::ConstraintsFromJson(value, processed_keys, error))
    return false;

  if (processed_keys->find(commands::attributes::kOneOf_Enum) ==
      processed_keys->end()) {
    // Process min/max constraints only if "enum" constraint wasn't already
    // specified.
    if (value->HasKey(commands::attributes::kNumeric_Min)) {
      auto constraint = LoadMinMaxConstraint<ConstraintMin<T>, T>(
          commands::attributes::kNumeric_Min, value, error);
      if (!constraint)
        return false;
      this->AddConstraint(std::move(constraint));
      this->RemoveConstraint(ConstraintType::OneOf);
      processed_keys->insert(commands::attributes::kNumeric_Min);
    }
    if (value->HasKey(commands::attributes::kNumeric_Max)) {
      auto constraint = LoadMinMaxConstraint<ConstraintMax<T>, T>(
          commands::attributes::kNumeric_Max, value, error);
      if (!constraint)
        return false;
      this->AddConstraint(std::move(constraint));
      this->RemoveConstraint(ConstraintType::OneOf);
      processed_keys->insert(commands::attributes::kNumeric_Max);
    }
  }

  return true;
}

// StringPropType -------------------------------------------------------------

bool StringPropType::ConstraintsFromJson(
    const base::DictionaryValue* value,
    std::set<std::string>* processed_keys,
    chromeos::ErrorPtr* error) {
  if (!_Base::ConstraintsFromJson(value, processed_keys, error))
    return false;

  if (processed_keys->find(commands::attributes::kOneOf_Enum) ==
      processed_keys->end()) {
    // Process min/max constraints only if "enum" constraint wasn't already
    // specified.
    if (value->HasKey(commands::attributes::kString_MinLength)) {
      auto constraint = LoadMinMaxConstraint<ConstraintStringLengthMin, int>(
          commands::attributes::kString_MinLength, value, error);
      if (!constraint)
        return false;
      AddConstraint(std::move(constraint));
      RemoveConstraint(ConstraintType::OneOf);
      processed_keys->insert(commands::attributes::kString_MinLength);
    }
    if (value->HasKey(commands::attributes::kString_MaxLength)) {
      auto constraint = LoadMinMaxConstraint<ConstraintStringLengthMax, int>(
          commands::attributes::kString_MaxLength, value, error);
      if (!constraint)
        return false;
      AddConstraint(std::move(constraint));
      RemoveConstraint(ConstraintType::OneOf);
      processed_keys->insert(commands::attributes::kString_MaxLength);
    }
  }
  return true;
}

void StringPropType::AddLengthConstraint(int min_len, int max_len) {
  InheritableAttribute<int> min_attr(min_len, false);
  InheritableAttribute<int> max_attr(max_len, false);
  AddConstraint(std::unique_ptr<ConstraintStringLengthMin>{
      new ConstraintStringLengthMin{min_attr}});
  AddConstraint(std::unique_ptr<ConstraintStringLengthMax>{
      new ConstraintStringLengthMax{max_attr}});
}

int StringPropType::GetMinLength() const {
  auto slc = static_cast<const ConstraintStringLength*>(
      GetConstraint(ConstraintType::StringLengthMin));
  return slc ? slc->limit_.value : 0;
}

int StringPropType::GetMaxLength() const {
  auto slc = static_cast<const ConstraintStringLength*>(
      GetConstraint(ConstraintType::StringLengthMax));
  return slc ? slc->limit_.value : std::numeric_limits<int>::max();
}

// ObjectPropType -------------------------------------------------------------

ObjectPropType::ObjectPropType()
    : object_schema_{ObjectSchema::Create(), false} {}

bool ObjectPropType::HasOverriddenAttributes() const {
  return PropType::HasOverriddenAttributes() ||
         !object_schema_.is_inherited;
}

std::unique_ptr<PropType> ObjectPropType::Clone() const {
  auto cloned = _Base::Clone();

  cloned->GetObject()->object_schema_.is_inherited =
      object_schema_.is_inherited;
  cloned->GetObject()->object_schema_.value = object_schema_.value->Clone();
  return cloned;
}

std::unique_ptr<base::Value> ObjectPropType::ToJson(
    bool full_schema, chromeos::ErrorPtr* error) const {
  std::unique_ptr<base::Value> value = PropType::ToJson(full_schema, error);
  if (value) {
    base::DictionaryValue* dict = nullptr;
    CHECK(value->GetAsDictionary(&dict)) << "Expecting a JSON object";
    if (!object_schema_.is_inherited || full_schema) {
      auto object_schema = object_schema_.value->ToJson(full_schema, error);
      if (!object_schema) {
        value.reset();
        return value;
      }
      dict->SetWithoutPathExpansion(commands::attributes::kObject_Properties,
                                    object_schema.release());
    }
  }
  return value;
}

bool ObjectPropType::ObjectSchemaFromJson(const base::DictionaryValue* value,
                                          const PropType* base_schema,
                                          std::set<std::string>* processed_keys,
                                          chromeos::ErrorPtr* error) {
  if (!_Base::ObjectSchemaFromJson(value, base_schema, processed_keys, error))
    return false;

  using commands::attributes::kObject_Properties;

  const ObjectSchema* base_object_schema = nullptr;
  if (base_schema)
    base_object_schema = base_schema->GetObject()->GetObjectSchemaPtr();

  const base::DictionaryValue* props = nullptr;
  if (value->GetDictionaryWithoutPathExpansion(kObject_Properties, &props)) {
    processed_keys->insert(kObject_Properties);
    std::unique_ptr<ObjectSchema> object_schema{new ObjectSchema};
    if (!object_schema->FromJson(props, base_object_schema, error)) {
      chromeos::Error::AddTo(error, FROM_HERE, errors::commands::kDomain,
                             errors::commands::kInvalidObjectSchema,
                             "Error parsing object property schema");
      return false;
    }
    object_schema_.value = std::move(object_schema);
    object_schema_.is_inherited = false;
  } else if (base_object_schema) {
    object_schema_.value = base_object_schema->Clone();
    object_schema_.is_inherited = true;
  } else {
    chromeos::Error::AddToPrintf(error, FROM_HERE, errors::commands::kDomain,
                                 errors::commands::kInvalidObjectSchema,
                                 "Object type definition must include the "
                                 "object schema ('%s' field not found)",
                                 kObject_Properties);
    return false;
  }

  return true;
}

void ObjectPropType::SetObjectSchema(
    std::unique_ptr<const ObjectSchema> schema) {
  object_schema_.value = std::move(schema);
  object_schema_.is_inherited = false;
}

}  // namespace buffet
