// Copyright 2019 The Chromium OS Authors. All rights reserved.
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

namespace {

base::StringPiece ToStringPiece(std::string_view sv) {
  return base::StringPiece(sv.data(), sv.length());
}

base::Value SaveAsJson(const ipp::Collection* coll);

// It saves a single value (at given index) from the attribute as JSON
// structure. The parameter "attr" cannot be nullptr, "index" must be correct.
base::Value SaveAsJson(const ipp::Attribute* attr, unsigned index) {
  CHECK(attr != nullptr);
  CHECK(index < attr->Size());
  switch (attr->Tag()) {
    case ipp::ValueTag::integer: {
      int vi;
      attr->GetValue(&vi, index);
      return base::Value(vi);
    }
    case ipp::ValueTag::boolean: {
      int vb;
      attr->GetValue(&vb, index);
      return base::Value(static_cast<bool>(vb));
    }
    case ipp::ValueTag::enum_: {
      std::string vs;
      attr->GetValue(&vs, index);
      if (vs.empty()) {
        int vi;
        attr->GetValue(&vi, index);
        return base::Value(vi);
      }
      return base::Value(vs);
    }
    case ipp::ValueTag::collection:
      return SaveAsJson(attr->GetCollection(index));
    case ipp::ValueTag::textWithLanguage:
    case ipp::ValueTag::nameWithLanguage: {
      ipp::StringWithLanguage vs;
      attr->GetValue(&vs, index);
      if (vs.language.empty())
        return base::Value(vs.value);
      base::Value::Dict obj;
      obj.Set("value", vs.value);
      obj.Set("language", vs.language);
      return base::Value(std::move(obj));
    }
    case ipp::ValueTag::textWithoutLanguage:
    case ipp::ValueTag::nameWithoutLanguage:
    case ipp::ValueTag::dateTime:
    case ipp::ValueTag::resolution:
    case ipp::ValueTag::rangeOfInteger:
    case ipp::ValueTag::octetString:
    case ipp::ValueTag::keyword:
    case ipp::ValueTag::uri:
    case ipp::ValueTag::uriScheme:
    case ipp::ValueTag::charset:
    case ipp::ValueTag::naturalLanguage:
    case ipp::ValueTag::mimeMediaType: {
      std::string vs;
      attr->GetValue(&vs, index);
      return base::Value(vs);
    }
    default:
      return base::Value();  // unknown type
  }
}

// It saves all attribute's values as JSON structure.
// The parameter "attr" cannot be nullptr.
base::Value SaveAsJson(const ipp::Attribute* attr) {
  CHECK(attr != nullptr);
  const unsigned size = attr->Size();
  if (size > 1) {
    base::Value::List arr;
    for (unsigned i = 0; i < size; ++i)
      arr.Append(SaveAsJson(attr, i));
    return base::Value(std::move(arr));
  } else {
    return SaveAsJson(attr, 0);
  }
}

// It saves a given Collection as JSON object.
// The parameter "coll" cannot be nullptr.
base::Value SaveAsJson(const ipp::Collection* coll) {
  CHECK(coll != nullptr);
  base::Value::Dict obj;
  auto attrs = coll->GetAllAttributes();

  for (auto a : attrs) {
    auto tag = a->Tag();
    if (!ipp::IsOutOfBand(tag)) {
      base::Value::Dict obj2;
      obj2.Set("type", ToStringPiece(ipp::ToStrView(tag)));
      obj2.Set("value", SaveAsJson(a));
      obj.Set(ToStringPiece(a->Name()), std::move(obj2));
    } else {
      obj.Set(ToStringPiece(a->Name()), ToStringPiece(ipp::ToStrView(tag)));
    }
  }

  return base::Value(std::move(obj));
}

// It saves all groups from given Package as JSON object.
base::Value SaveAsJson(const ipp::Frame& pkg) {
  base::Value::Dict obj;
  for (ipp::GroupTag gt : ipp::kGroupTags) {
    auto groups = pkg.GetGroups(gt);
    if (groups.empty())
      continue;
    if (groups.size() > 1) {
      base::Value arr(base::Value::Type::LIST);
      for (auto g : groups)
        arr.Append(SaveAsJson(g));
      obj.Set(ToString(gt), std::move(arr));
    } else {
      obj.Set(ToString(gt), SaveAsJson(groups.front()));
    }
  }
  return base::Value(std::move(obj));
}

// Saves given logs as JSON array.
base::Value SaveAsJson(const ipp::ParsingResults& log) {
  base::Value::List arr;
  for (const auto& l : log.errors) {
    base::Value::Dict obj;
    obj.Set("message", l.message);
    if (!l.frame_context.empty())
      obj.Set("frame_context", l.frame_context);
    if (!l.parser_context.empty())
      obj.Set("parser_context", l.parser_context);
    arr.Append(std::move(obj));
  }
  return base::Value(std::move(arr));
}

}  // namespace

bool ConvertToJson(const ipp::Frame& response,
                   const ipp::ParsingResults& log,
                   bool compressed_json,
                   std::string* json) {
  // Build structure.
  base::Value::Dict doc;
  doc.Set("status", ipp::ToString(response.StatusCode()));
  if (!log.errors.empty()) {
    doc.Set("parsing_logs", SaveAsJson(log));
  }
  doc.Set("response", SaveAsJson(response));
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
