// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipp_in_json.h"

#include <memory>
#include <utility>

#include <base/check.h>
#include <base/json/json_writer.h>
#include <base/values.h>
#include <chromeos/libipp/attribute.h>
#include <chromeos/libipp/frame.h>
#include <chromeos/libipp/ipp_enums.h>
#include <chromeos/libipp/parser.h>

namespace {

base::Value SaveAsJson(const ipp::Collection& coll,
                       const std::string& filter,
                       bool expanded);

// Converts `value` from the attribute `attr` to base::Value.
template <typename ValueType>
base::Value SaveValueAsJson(const ipp::Attribute& attr,
                            const ValueType& value,
                            bool expanded) {
  return base::Value(ipp::ToString(value));
}

template <>
base::Value SaveValueAsJson<int32_t>(const ipp::Attribute& attr,
                                     const int32_t& value,
                                     bool expanded) {
  if (attr.Tag() == ipp::ValueTag::boolean) {
    return base::Value(static_cast<bool>(value));
  }
  if (attr.Tag() == ipp::ValueTag::enum_) {
    ipp::AttrName attrName;
    if (ipp::FromString(std::string(attr.Name()), &attrName)) {
      return base::Value(ipp::ToString(attrName, value));
    }
  }
  return base::Value(value);
}

template <>
base::Value SaveValueAsJson<std::string>(const ipp::Attribute& attr,
                                         const std::string& value,
                                         bool expanded) {
  return base::Value(value);
}

template <>
base::Value SaveValueAsJson<ipp::StringWithLanguage>(
    const ipp::Attribute& attr,
    const ipp::StringWithLanguage& value,
    bool expanded) {
  if (expanded) {
    base::Value::Dict obj;
    obj.Set("value", value.value);
    obj.Set("language", value.language);
    return base::Value(std::move(obj));
  } else {
    return base::Value(value.value);
  }
}

// Converts all values from `attr` to base::Value. The type of values must match
// `ValueType`.
template <typename ValueType>
base::Value SaveValuesAsJsonTyped(const ipp::Attribute& attr, bool expanded) {
  std::vector<ValueType> values;
  attr.GetValues(values);
  if (values.size() > 1) {
    base::Value::List arr;
    for (size_t i = 0; i < values.size(); ++i) {
      arr.Append(SaveValueAsJson(attr, values[i], expanded));
    }
    return base::Value(std::move(arr));
  } else {
    return SaveValueAsJson(attr, values.at(0), expanded);
  }
}

template <>
base::Value SaveValuesAsJsonTyped<const ipp::Collection&>(
    const ipp::Attribute& attr, bool expanded) {
  ipp::ConstCollsView colls = attr.Colls();
  if (colls.size() > 1) {
    base::Value::List arr;
    for (const ipp::Collection& coll : colls) {
      // Don't filter inner collection attributes.  The outer collection itself
      // would have already been skipped if it didn't match the user's filter.
      arr.Append(SaveAsJson(coll, "", expanded));
    }
    return base::Value(std::move(arr));
  } else {
    // Don't filter inner collection attributes.  The outer collection itself
    // would have already been skipped if it didn't match the user's filter.
    return SaveAsJson(colls[0], "", expanded);
  }
}

// It saves all attribute's values as JSON structure.
base::Value SaveValuesAsJson(const ipp::Attribute& attr, bool expanded) {
  switch (attr.Tag()) {
    case ipp::ValueTag::textWithLanguage:
    case ipp::ValueTag::nameWithLanguage:
      return SaveValuesAsJsonTyped<ipp::StringWithLanguage>(attr, expanded);
    case ipp::ValueTag::dateTime:
      return SaveValuesAsJsonTyped<ipp::DateTime>(attr, expanded);
    case ipp::ValueTag::resolution:
      return SaveValuesAsJsonTyped<ipp::Resolution>(attr, expanded);
    case ipp::ValueTag::rangeOfInteger:
      return SaveValuesAsJsonTyped<ipp::RangeOfInteger>(attr, expanded);
    case ipp::ValueTag::collection:
      return SaveValuesAsJsonTyped<const ipp::Collection&>(attr, expanded);
    default:
      if (ipp::IsInteger(attr.Tag())) {
        return SaveValuesAsJsonTyped<int32_t>(attr, expanded);
      }
      return SaveValuesAsJsonTyped<std::string>(attr, expanded);
  }
}

// It saves a given Collection as JSON object.
base::Value SaveAsJson(const ipp::Collection& coll,
                       const std::string& filter,
                       bool expanded) {
  base::Value::Dict obj;

  for (const ipp::Attribute& a : coll) {
    if (!filter.empty() && a.Name().find(filter) == std::string::npos) {
      continue;
    }
    auto tag = a.Tag();
    if (!ipp::IsOutOfBand(tag)) {
      if (expanded) {
        base::Value::Dict obj2;
        obj2.Set("type", ipp::ToStrView(tag));
        obj2.Set("value", SaveValuesAsJson(a, true));
        obj.Set(a.Name(), std::move(obj2));
      } else {
        obj.Set(a.Name(), SaveValuesAsJson(a, false));
      }
    } else {
      obj.Set(a.Name(), ipp::ToStrView(tag));
    }
  }

  return base::Value(std::move(obj));
}

// It saves one group as a JSON object.
base::Value SaveAsJson(ipp::ConstCollsView groups,
                       const std::string& filter,
                       bool expanded) {
  if (groups.size() > 1) {
    base::Value::List arr;
    for (const ipp::Collection& g : groups) {
      arr.Append(SaveAsJson(g, filter, expanded));
    }
    return base::Value(std::move(arr));
  } else {
    return SaveAsJson(groups[0], filter, expanded);
  }
}

// It saves all groups from given Package as JSON object.
base::Value SaveAsJson(const ipp::Frame& pkg,
                       const std::string& filter,
                       bool expanded) {
  base::Value::Dict obj;
  for (ipp::GroupTag gt : ipp::kGroupTags) {
    auto groups = pkg.Groups(gt);
    if (groups.empty()) {
      continue;
    }
    if (gt == ipp::GroupTag::operation_attributes && !filter.empty()) {
      // Skip operation-attributes group if the user is filtering because the
      // values returned will never be of interest.
      continue;
    }
    // Don't apply the output filter to the unsupported-attributes group because
    // the user may have no other way to see that their request was not
    // processed as expected.
    const std::string& apply_filter =
        gt != ipp::GroupTag::unsupported_attributes ? filter : "";
    obj.Set(ToString(gt), SaveAsJson(groups, apply_filter, expanded));
  }
  return base::Value(std::move(obj));
}

// Saves given logs as JSON array.
base::Value SaveAsJson(const ipp::SimpleParserLog& log) {
  base::Value::List arr;
  for (const auto& l : log.Errors()) {
    arr.Append(base::Value(ipp::ToString(l)));
  }
  return base::Value(std::move(arr));
}

}  // namespace

bool ConvertToJson(const ipp::Frame& response,
                   const ipp::SimpleParserLog& log,
                   const std::string& filter,
                   bool compressed_json,
                   std::string* json) {
  // Build structure.
  base::Value::Dict doc;
  doc.Set("status", ipp::ToString(response.StatusCode()));
  if (!log.Errors().empty()) {
    doc.Set("parsing_logs", SaveAsJson(log));
  }
  doc.Set("response", SaveAsJson(response, filter, /*expanded=*/true));
  // Convert to JSON.
  bool result;
  if (compressed_json) {
    result = base::JSONWriter::Write(doc, json);
  } else {
    const int options = base::JSONWriter::OPTIONS_PRETTY_PRINT;
    result = base::JSONWriter::WriteWithOptions(doc, options, json);
  }
  return result;
}

bool ConvertToSimpleJson(const ipp::Frame& response,
                         const ipp::SimpleParserLog& log,
                         const std::string& filter,
                         std::string* json) {
  // Build structure.
  base::Value::Dict doc;
  doc.Set("status", ipp::ToString(response.StatusCode()));
  if (!log.Errors().empty()) {
    doc.Set("parsing_logs", SaveAsJson(log));
  }

  // Only include printer-attributes and unsupported-attributes in the output.
  auto groups = response.Groups(ipp::GroupTag::printer_attributes);
  if (!groups.empty()) {
    doc.Set("printer-attributes",
            SaveAsJson(groups, filter, /*expanded=*/false));
  }
  groups = response.Groups(ipp::GroupTag::unsupported_attributes);
  if (!groups.empty()) {
    doc.Set("unsupported-attributes",
            SaveAsJson(groups, "", /*expanded=*/false));
  }

  // Convert to JSON.
  const int options = base::JSONWriter::OPTIONS_PRETTY_PRINT;
  return base::JSONWriter::WriteWithOptions(doc, options, json);
}
