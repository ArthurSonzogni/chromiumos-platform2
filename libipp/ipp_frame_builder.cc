// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libipp/ipp_frame_builder.h"

#include <algorithm>
#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "frame.h"
#include "libipp/ipp_attribute.h"
#include "libipp/ipp_encoding.h"

namespace ipp {

namespace {

// Saves 1-,2- or 4-bytes integer to buf using two's-complement binary encoding.
// The "buf" parameter is always resized to BytesCount. Returns false when
// given integer is out of range. In this case 0 is saved to buf.
template <size_t BytesCount>
bool SaveInteger(int v, std::vector<uint8_t>* buf) {
  buf->resize(BytesCount);
  uint8_t* ptr = buf->data();
  if (!WriteInteger<BytesCount>(&ptr, v)) {
    buf->clear();
    buf->resize(BytesCount, 0);
    return false;
  }
  return true;
}

// Saves simple string to buf, buf is resized accordingly.
void SaveOctetString(const std::string& s, std::vector<uint8_t>* buf) {
  buf->assign(s.begin(), s.end());
}
void SaveOctetString(std::string_view s, std::vector<uint8_t>* buf) {
  buf->assign(s.begin(), s.end());
}

// Writes textWithLanguage/nameWithLanguage (see [rfc8010]) to buf, which is
// resized accordingly. Returns false if given string(s) is too long. In this
// case an empty string is saved to the buffer.
bool SaveStringWithLanguage(const ipp::StringWithLanguage& s,
                            std::vector<uint8_t>* buf) {
  buf->clear();
  buf->resize(4 + s.value.size() + s.language.size());
  uint8_t* ptr = buf->data();
  if (!WriteInteger<2>(&ptr, s.language.size())) {
    std::vector<uint8_t> empty_buffer(4, 0);
    buf->swap(empty_buffer);
    return false;
  }
  ptr = std::copy(s.language.begin(), s.language.end(), ptr);
  if (!WriteInteger<2>(&ptr, s.value.size())) {
    std::vector<uint8_t> empty_buffer(4, 0);
    buf->swap(empty_buffer);
    return false;
  }
  std::copy(s.value.begin(), s.value.end(), ptr);
  return true;
}

// Saves dateTime (see [rfc8010]) to buf, which is resized accordingly. Returns
// false when the given dateTime is incorrect; in this case |buf| is set to
// 2000/1/1 00:00:00 +00:00.
bool SaveDateTime(const ipp::DateTime& v, std::vector<uint8_t>* buf) {
  buf->resize(11);
  uint8_t* ptr = buf->data();
  if (WriteInteger<2>(&ptr, v.year) && WriteInteger<1>(&ptr, v.month) &&
      WriteInteger<1>(&ptr, v.day) && WriteInteger<1>(&ptr, v.hour) &&
      WriteInteger<1>(&ptr, v.minutes) && WriteInteger<1>(&ptr, v.seconds) &&
      WriteInteger<1>(&ptr, v.deci_seconds) &&
      WriteInteger<1>(&ptr, v.UTC_direction) &&
      WriteInteger<1>(&ptr, v.UTC_hours) &&
      WriteInteger<1>(&ptr, v.UTC_minutes))
    return true;
  buf->clear();
  buf->resize(11, 0);
  ptr = buf->data();
  WriteInteger<int16_t>(&ptr, 2000);
  (*buf)[8] = '+';
  return false;
}

// Writes resolution (see [rfc8010]) to buf, which is resized accordingly.
// Returns false when given value is incorrect; in this case |buf| is set to
// 256x256dpi.
bool SaveResolution(const ipp::Resolution& v, std::vector<uint8_t>* buf) {
  buf->resize(9);
  uint8_t* ptr = buf->data();
  if (WriteInteger<4>(&ptr, v.xres) && WriteInteger<4>(&ptr, v.yres) &&
      (v.units == ipp::Resolution::kDotsPerCentimeter ||
       v.units == ipp::Resolution::kDotsPerInch)) {
    WriteInteger<int8_t>(&ptr, v.units);
    return true;
  }
  buf->clear();
  // set to 256x256 dpi
  buf->resize(9, 0);
  (*buf)[2] = (*buf)[6] = 1;
  (*buf)[8] = ipp::Resolution::kDotsPerInch;
  return false;
}

// Writes rangeOfInteger (see [rfc8010]) to buf, which is resized accordingly.
// Returns false when given value is incorrect; in this case |buf| is set to 0.
bool SaveRangeOfInteger(const ipp::RangeOfInteger& v,
                        std::vector<uint8_t>* buf) {
  buf->resize(8);
  uint8_t* ptr = buf->data();
  if (WriteInteger<4>(&ptr, v.min_value) && WriteInteger<4>(&ptr, v.max_value))
    return true;
  buf->clear();
  buf->resize(8, 0);
  return false;
}

}  //  namespace

void FrameBuilder::LogFrameBuilderError(const std::string& message) {
  Log l;
  l.message = "Error when building frame: " + message + ".";
  errors_->push_back(l);
}

void FrameBuilder::SaveAttrValue(const Attribute* attr,
                                 size_t index,
                                 uint8_t* tag,
                                 std::vector<uint8_t>* buf) {
  *tag = static_cast<uint8_t>(attr->Tag());
  bool result = true;
  switch (attr->Tag()) {
    case ValueTag::boolean: {
      int v = 0;
      attr->GetValue(&v, index);
      if (v != 0)
        v = 1;
      result = SaveInteger<1>(v, buf);
      break;
    }
    case ValueTag::integer:
    case ValueTag::enum_: {
      int v = 0;
      attr->GetValue(&v, index);
      result = SaveInteger<4>(v, buf);
      break;
    }
    case ValueTag::dateTime: {
      DateTime v;
      attr->GetValue(&v, index);
      result = SaveDateTime(v, buf);
      break;
    }
    case ValueTag::resolution: {
      Resolution v;
      attr->GetValue(&v, index);
      result = SaveResolution(v, buf);
      break;
    }
    case ValueTag::rangeOfInteger: {
      RangeOfInteger v;
      attr->GetValue(&v, index);
      result = SaveRangeOfInteger(v, buf);
      break;
    }
    case ValueTag::textWithLanguage: {
      StringWithLanguage s;
      attr->GetValue(&s, index);
      if (s.language.empty()) {
        *tag = static_cast<uint8_t>(ValueTag::textWithoutLanguage);
        SaveOctetString(s, buf);
      } else {
        result = SaveStringWithLanguage(s, buf);
      }
    } break;
    case ValueTag::nameWithLanguage: {
      StringWithLanguage s;
      attr->GetValue(&s, index);
      if (s.language.empty()) {
        *tag = static_cast<uint8_t>(ValueTag::nameWithoutLanguage);
        SaveOctetString(s, buf);
      } else {
        result = SaveStringWithLanguage(s, buf);
      }
    } break;
    case ValueTag::octetString:
    case ValueTag::textWithoutLanguage:
    case ValueTag::nameWithoutLanguage:
    case ValueTag::keyword:
    case ValueTag::uri:
    case ValueTag::uriScheme:
    case ValueTag::charset:
    case ValueTag::naturalLanguage:
    case ValueTag::mimeMediaType: {
      std::string s = "";
      attr->GetValue(&s, index);
      SaveOctetString(s, buf);
      break;
    }
    default:
      LogFrameBuilderError(
          "Internal error: cannot recognize value type of the attribute " +
          std::string(attr->Name()));
      buf->clear();
      break;
  }
  if (!result)
    LogFrameBuilderError("Incorrect value of the attribute " +
                         std::string(attr->Name()) +
                         ". Default value was written instead");
}

void FrameBuilder::SaveCollection(const Collection* coll,
                                  std::list<TagNameValue>* data_chunks) {
  // get list of all attributes
  std::vector<const Attribute*> attrs = coll->GetAllAttributes();
  // save the attributes
  for (const Attribute* attr : attrs) {
    TagNameValue tnv;
    tnv.tag = memberAttrName_value_tag;
    tnv.name.clear();
    SaveOctetString(attr->Name(), &tnv.value);
    data_chunks->push_back(tnv);
    if (IsOutOfBand(attr->Tag())) {
      tnv.tag = static_cast<uint8_t>(attr->Tag());
      tnv.value.clear();
      data_chunks->push_back(tnv);
    } else {
      // standard values (one or more)
      for (size_t val_index = 0; val_index < attr->Size(); ++val_index) {
        if (attr->Tag() == ValueTag::collection) {
          tnv.tag = begCollection_value_tag;
          tnv.value.clear();
          data_chunks->push_back(tnv);
          SaveCollection(attr->GetCollection(val_index), data_chunks);
          tnv.tag = endCollection_value_tag;
          tnv.value.clear();
        } else {
          SaveAttrValue(attr, val_index, &tnv.tag, &tnv.value);
        }
        data_chunks->push_back(tnv);
      }
    }
  }
}

void FrameBuilder::SaveGroup(const Collection* coll,
                             std::list<TagNameValue>* data_chunks) {
  // get list of all attributes
  std::vector<const Attribute*> attrs = coll->GetAllAttributes();
  // save the attributes
  for (const Attribute* attr : attrs) {
    TagNameValue tnv;
    SaveOctetString(attr->Name(), &tnv.name);
    if (IsOutOfBand(attr->Tag())) {
      tnv.tag = static_cast<uint8_t>(attr->Tag());
      tnv.value.clear();
      data_chunks->push_back(tnv);
      continue;
    }
    for (size_t val_index = 0; val_index < attr->Size(); ++val_index) {
      if (attr->Tag() == ValueTag::collection) {
        tnv.tag = begCollection_value_tag;
        tnv.value.clear();
        data_chunks->push_back(tnv);
        SaveCollection(attr->GetCollection(val_index), data_chunks);
        tnv.tag = endCollection_value_tag;
        tnv.name.clear();
        tnv.value.clear();
      } else {
        SaveAttrValue(attr, val_index, &tnv.tag, &tnv.value);
      }
      data_chunks->push_back(tnv);
      tnv.name.clear();
    }
  }
}

void FrameBuilder::BuildFrameFromPackage(const Frame* package) {
  frame_->groups_tags_.clear();
  frame_->groups_content_.clear();
  // save frame data
  std::vector<std::pair<GroupTag, const Collection*>> groups =
      package->GetGroups();
  for (std::pair<GroupTag, const Collection*> grp : groups) {
    frame_->groups_tags_.push_back(grp.first);
    frame_->groups_content_.resize(frame_->groups_tags_.size());
    SaveGroup(grp.second, &(frame_->groups_content_.back()));
  }
  frame_->data_ = package->Data();
}

void FrameBuilder::WriteFrameToBuffer(uint8_t* ptr) {
  WriteUnsigned(&ptr, frame_->version_);
  WriteInteger<2>(&ptr, frame_->operation_id_or_status_code_);
  WriteInteger<4>(&ptr, frame_->request_id_);
  for (size_t i = 0; i < frame_->groups_tags_.size(); ++i) {
    // write a group to the buffer
    WriteUnsigned(&ptr, static_cast<uint8_t>(frame_->groups_tags_[i]));
    WriteTNVsToBuffer(frame_->groups_content_[i], &ptr);
  }
  WriteUnsigned(&ptr, end_of_attributes_tag);
  std::copy(frame_->data_.begin(), frame_->data_.end(), ptr);
}

void FrameBuilder::WriteTNVsToBuffer(const std::list<TagNameValue>& tnvs,
                                     uint8_t** ptr) {
  for (auto& tnv : tnvs) {
    WriteUnsigned(ptr, tnv.tag);
    WriteInteger(ptr, static_cast<int16_t>(tnv.name.size()));
    *ptr = std::copy(tnv.name.begin(), tnv.name.end(), *ptr);
    WriteInteger(ptr, static_cast<int16_t>(tnv.value.size()));
    *ptr = std::copy(tnv.value.begin(), tnv.value.end(), *ptr);
  }
}

std::size_t FrameBuilder::GetFrameLength() const {
  // Header has always 8 bytes (ipp_version + operation_id/status + request_id).
  size_t length = 8;
  // The header is followed by a list of groups.
  for (const auto& tnvs : frame_->groups_content_) {
    // Each group starts with 1-byte group-tag ...
    length += 1;
    // ... and consists of list of tag-name-value.
    for (const auto& tnv : tnvs)
      // Tag + name_size + name + value_size + value.
      length += (1 + 2 + tnv.name.size() + 2 + tnv.value.size());
  }
  // end-of-attributes-tag + blob_with_data.
  length += 1 + frame_->data_.size();
  return length;
}

}  // namespace ipp
