// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frame.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "ipp_base.h"
#include "ipp_frame.h"
#include "ipp_frame_builder.h"
#include "ipp_parser.h"

namespace ipp {

// Min and max valid value of GroupTag. Keep in mind that in this range includes
// also invalid value 0x03.
constexpr GroupTag kMinGroupTag = static_cast<GroupTag>(0x01);
constexpr GroupTag kMaxGroupTag = static_cast<GroupTag>(0x0f);

constexpr size_t kMaxPayloadSize = 256 * 1024 * 1024;

namespace {

void SetCharsetAndLanguageAttributes(Frame* frame) {
  Collection* grp;
  frame->AddGroup(ipp::GroupTag::operation_attributes, &grp);
  auto attr = grp->AddUnknownAttribute("attributes-charset", true,
                                       ipp::AttrType::charset);
  attr->SetValue("utf-8");
  attr = grp->AddUnknownAttribute("attributes-natural-language", true,
                                  ipp::AttrType::naturalLanguage);
  attr->SetValue("en-us");
}

struct Converter {
  std::vector<Log> log;
  FrameData frame_data;
  FrameBuilder builder{&frame_data, &log};
  Converter(Version version,
            uint16_t operation_id_or_status_code,
            int32_t request_id,
            const Package* package) {
    frame_data.major_version_number_ = (static_cast<uint16_t>(version) >> 8);
    frame_data.minor_version_number_ = (static_cast<uint16_t>(version) & 0xffu);
    frame_data.operation_id_or_status_code_ = operation_id_or_status_code;
    frame_data.request_id_ = request_id;
    builder.BuildFrameFromPackage(package);
  }
};

}  // namespace

Frame::Frame()
    : version_(static_cast<ipp::Version>(0)),
      operation_id_or_status_code_(0),
      request_id_(0) {}

Frame::Frame(Version ver,
             Operation operation_id,
             int32_t request_id,
             bool set_charset)
    : version_(ver),
      operation_id_or_status_code_(static_cast<uint16_t>(operation_id)),
      request_id_(request_id) {
  if (set_charset) {
    SetCharsetAndLanguageAttributes(this);
  }
}

Frame::Frame(Version ver,
             Status status_code,
             int32_t request_id,
             bool set_charset)
    : version_(ver),
      operation_id_or_status_code_(static_cast<uint16_t>(status_code)),
      request_id_(request_id) {
  if (set_charset) {
    SetCharsetAndLanguageAttributes(this);
  }
}

Frame::Frame(const uint8_t* buffer, size_t size, ParsingResults* result) {
  if (buffer == nullptr) {
    version_ = static_cast<ipp::Version>(0);
    operation_id_or_status_code_ = 0;
    request_id_ = 0;
    if (result != nullptr) {
      result->errors.push_back(Log({"Buffer is nullptr"}));
      result->whole_buffer_was_parsed = false;
    }
    return;
  }
  std::vector<Log> log;
  FrameData frame_data;
  Parser parser(&frame_data, &log);
  const bool completed1 = parser.ReadFrameFromBuffer(buffer, buffer + size);
  const bool completed2 = parser.SaveFrameToPackage(false, &package_);
  if (result) {
    result->whole_buffer_was_parsed = completed1 && completed2;
    result->errors.swap(log);
  }
  uint16_t ver = frame_data.major_version_number_;
  ver <<= 8;
  ver += frame_data.minor_version_number_;
  version_ = static_cast<Version>(ver);
  operation_id_or_status_code_ = frame_data.operation_id_or_status_code_;
  request_id_ = frame_data.request_id_;
}

size_t Frame::GetLength() const {
  std::vector<Log> log;
  FrameData frame_data;
  FrameBuilder builder(&frame_data, &log);
  builder.BuildFrameFromPackage(&package_);
  return builder.GetFrameLength();
}

size_t Frame::SaveToBuffer(uint8_t* buffer, size_t buffer_length) const {
  Converter converter(version_, operation_id_or_status_code_, request_id_,
                      &package_);
  const size_t length = converter.builder.GetFrameLength();
  if (length > buffer_length) {
    return 0;
  }
  converter.builder.WriteFrameToBuffer(buffer);
  return length;
}

std::vector<uint8_t> Frame::SaveToBuffer() const {
  Converter converter(version_, operation_id_or_status_code_, request_id_,
                      &package_);
  std::vector<uint8_t> out(converter.builder.GetFrameLength());
  converter.builder.WriteFrameToBuffer(out.data());
  return out;
}

Version Frame::VersionNumber() const {
  return version_;
}

Version& Frame::VersionNumber() {
  return version_;
}

uint16_t& Frame::OperationIdOrStatusCode() {
  return operation_id_or_status_code_;
}

Operation Frame::OperationId() const {
  return static_cast<Operation>(operation_id_or_status_code_);
}

Status Frame::StatusCode() const {
  return static_cast<Status>(operation_id_or_status_code_);
}

int32_t& Frame::RequestId() {
  return request_id_;
}

int32_t Frame::RequestId() const {
  return request_id_;
}

const std::vector<uint8_t>& Frame::Data() const {
  return package_.Data();
}

std::vector<uint8_t> Frame::TakeData() {
  std::vector<uint8_t> data;
  data.swap(package_.Data());
  return data;
}

Code Frame::SetData(std::vector<uint8_t>&& data) {
  if (data.size() > kMaxPayloadSize) {
    return Code::kDataTooLong;
  }
  std::vector<uint8_t> data2;
  data2.swap(data);
  package_.Data() = std::move(data2);
  return Code::kOK;
}

std::vector<Collection*> Frame::GetGroups(GroupTag tag) {
  std::vector<Collection*> out;
  Group* grp = package_.GetGroup(tag);
  if (grp != nullptr) {
    out.resize(grp->GetSize());
    for (size_t i = 0; i < out.size(); ++i) {
      out[i] = grp->GetCollection(i);
    }
  }
  return out;
}

std::vector<const Collection*> Frame::GetGroups(GroupTag tag) const {
  std::vector<const Collection*> out;
  const Group* grp = package_.GetGroup(tag);
  if (grp != nullptr) {
    out.resize(grp->GetSize());
    for (size_t i = 0; i < out.size(); ++i) {
      out[i] = grp->GetCollection(i);
    }
  }
  return out;
}

Collection* Frame::GetGroup(GroupTag tag, size_t index) {
  Group* grp = package_.GetGroup(tag);
  if (grp != nullptr) {
    return grp->GetCollection(index);
  }
  return nullptr;
}

const Collection* Frame::GetGroup(GroupTag tag, size_t index) const {
  const Group* grp = package_.GetGroup(tag);
  if (grp != nullptr) {
    return grp->GetCollection(index);
  }
  return nullptr;
}

Code Frame::AddGroup(GroupTag tag, Collection** new_group) {
  if (tag < kMinGroupTag || tag > kMaxGroupTag ||
      static_cast<int>(tag) == 0x03) {
    return Code::kInvalidGroupTag;
  }
  auto grp = package_.AddUnknownGroup(tag, true);
  if (grp == nullptr) {
    grp = package_.GetGroup(tag);
    if (grp == nullptr) {
      return Code::kTooManyGroups;
    }
  }
  auto size = grp->GetSize();
  grp->Resize(size + 1);
  if (new_group) {
    *new_group = grp->GetCollection(size);
  }
  return Code::kOK;
}

}  // namespace ipp
