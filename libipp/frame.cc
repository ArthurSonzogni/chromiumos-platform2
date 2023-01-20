// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frame.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "ipp_frame.h"
#include "ipp_frame_builder.h"
#include "ipp_parser.h"

namespace ipp {

constexpr size_t kMaxPayloadSize = 256 * 1024 * 1024;

namespace {

void SetCharsetAndLanguageAttributes(Frame* frame) {
  Collection* grp;
  frame->AddGroup(ipp::GroupTag::operation_attributes, &grp);
  grp->AddAttr("attributes-charset", ValueTag::charset, "utf-8");
  grp->AddAttr("attributes-natural-language", ValueTag::naturalLanguage,
               "en-us");
}

struct Converter {
  std::vector<Log> log;
  FrameData frame_data;
  FrameBuilder builder{&frame_data, &log};
  Converter(Version version,
            uint16_t operation_id_or_status_code,
            int32_t request_id,
            const Frame* package) {
    frame_data.version_ = static_cast<uint16_t>(version);
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

Frame::Frame(Operation operation_id,
             Version version_number,
             int32_t request_id,
             bool set_localization_en_us)
    : version_(version_number),
      operation_id_or_status_code_(static_cast<uint16_t>(operation_id)),
      request_id_(request_id) {
  if (set_localization_en_us) {
    SetCharsetAndLanguageAttributes(this);
  }
}

Frame::Frame(Status status_code,
             Version version_number,
             int32_t request_id,
             bool set_localization_en_us_and_status_message)
    : version_(version_number),
      operation_id_or_status_code_(static_cast<uint16_t>(status_code)),
      request_id_(request_id) {
  if (set_localization_en_us_and_status_message) {
    SetCharsetAndLanguageAttributes(this);
    Collection* grp = this->GetGroup(GroupTag::operation_attributes);
    grp->AddAttr("status-message", ValueTag::textWithoutLanguage,
                 ToString(status_code));
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
  const bool completed2 = parser.SaveFrameToPackage(false, this);
  if (result) {
    result->whole_buffer_was_parsed = completed1 && completed2;
    result->errors.swap(log);
  }
  version_ = static_cast<Version>(frame_data.version_);
  operation_id_or_status_code_ = frame_data.operation_id_or_status_code_;
  request_id_ = frame_data.request_id_;
}

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

Frame::~Frame() {
  for (std::pair<GroupTag, Collection*>& group : groups_) {
    delete group.second;
  }
}

size_t Frame::GetLength() const {
  std::vector<Log> log;
  FrameData frame_data;
  FrameBuilder builder(&frame_data, &log);
  builder.BuildFrameFromPackage(this);
  return builder.GetFrameLength();
}

size_t Frame::SaveToBuffer(uint8_t* buffer, size_t buffer_length) const {
  Converter converter(version_, operation_id_or_status_code_, request_id_,
                      this);
  const size_t length = converter.builder.GetFrameLength();
  if (length > buffer_length) {
    return 0;
  }
  converter.builder.WriteFrameToBuffer(buffer);
  return length;
}

std::vector<uint8_t> Frame::SaveToBuffer() const {
  Converter converter(version_, operation_id_or_status_code_, request_id_,
                      this);
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

int16_t Frame::OperationIdOrStatusCode() const {
  return operation_id_or_status_code_;
}

int16_t& Frame::OperationIdOrStatusCode() {
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
  return data_;
}

std::vector<uint8_t> Frame::TakeData() {
  std::vector<uint8_t> data;
  data.swap(data_);
  return data;
}

Code Frame::SetData(std::vector<uint8_t>&& data) {
  if (data.size() > kMaxPayloadSize) {
    return Code::kDataTooLong;
  }
  data_ = std::move(data);
  return Code::kOK;
}

CollectionsView Frame::Groups(GroupTag tag) {
  if (IsValid(tag)) {
    return CollectionsView(groups_by_tag_[static_cast<size_t>(tag)]);
  }
  // groups_by_tag_[0] is always empty, since IsValid(0) == false.
  return CollectionsView(groups_by_tag_[0]);
}

ConstCollectionsView Frame::Groups(GroupTag tag) const {
  if (IsValid(tag)) {
    return ConstCollectionsView(groups_by_tag_[static_cast<size_t>(tag)]);
  }
  return ConstCollectionsView(groups_by_tag_[0]);
}

std::vector<std::pair<GroupTag, Collection*>> Frame::GetGroups() {
  return groups_;
}

std::vector<std::pair<GroupTag, const Collection*>> Frame::GetGroups() const {
  return std::vector<std::pair<GroupTag, const Collection*>>(groups_.begin(),
                                                             groups_.end());
}

std::vector<Collection*> Frame::GetGroups(GroupTag tag) {
  const size_t group_tag = static_cast<size_t>(tag);
  if (group_tag >= groups_by_tag_.size())
    return {};
  return groups_by_tag_[group_tag];
}

std::vector<const Collection*> Frame::GetGroups(GroupTag tag) const {
  const size_t group_tag = static_cast<size_t>(tag);
  if (group_tag >= groups_by_tag_.size())
    return {};
  return std::vector<const Collection*>(groups_by_tag_[group_tag].cbegin(),
                                        groups_by_tag_[group_tag].cend());
}

Collection* Frame::GetGroup(GroupTag tag, size_t index) {
  const size_t group_tag = static_cast<size_t>(tag);
  if (group_tag >= groups_by_tag_.size())
    return nullptr;
  if (index >= groups_by_tag_[group_tag].size())
    return nullptr;
  return groups_by_tag_[group_tag][index];
}

const Collection* Frame::GetGroup(GroupTag tag, size_t index) const {
  const size_t group_tag = static_cast<size_t>(tag);
  if (group_tag >= groups_by_tag_.size())
    return nullptr;
  if (index >= groups_by_tag_[group_tag].size())
    return nullptr;
  return groups_by_tag_[group_tag][index];
}

Code Frame::AddGroup(GroupTag tag, Collection** new_group) {
  if (!IsValid(tag)) {
    return Code::kInvalidGroupTag;
  }
  if (groups_.size() >= kMaxCountOfAttributeGroups) {
    return Code::kTooManyGroups;
  }
  Collection* coll = new Collection;
  groups_.emplace_back(tag, coll);
  groups_by_tag_[static_cast<size_t>(tag)].push_back(coll);
  if (new_group) {
    *new_group = coll;
  }
  return Code::kOK;
}

}  // namespace ipp
