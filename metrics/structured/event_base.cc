// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/structured/event_base.h"

#include "metrics/structured/recorder.h"

namespace metrics {
namespace structured {

EventBase::EventBase(uint64_t event_name_hash,
                     uint64_t project_name_hash,
                     IdType id_type,
                     StructuredEventProto_EventType event_type)
    : event_name_hash_(event_name_hash),
      project_name_hash_(project_name_hash),
      id_type_(id_type),
      event_type_(event_type) {}
EventBase::EventBase(const EventBase& other) = default;
EventBase::~EventBase() = default;

EventBase::Metric::Metric(uint64_t name_hash, MetricType type)
    : name_hash(name_hash), type(type) {}
EventBase::Metric::~Metric() = default;

bool EventBase::Record() {
  return Recorder::GetInstance()->Record(*this);
}

void EventBase::AddHmacMetric(uint64_t name_hash, const std::string& value) {
  Metric metric(name_hash, MetricType::kHmac);
  metric.hmac_value = value;
  metrics_.push_back(metric);
}

void EventBase::AddIntMetric(uint64_t name_hash, int64_t value) {
  Metric metric(name_hash, MetricType::kInt);
  metric.int_value = value;
  metrics_.push_back(metric);
}

void EventBase::AddRawStringMetric(uint64_t name_hash,
                                   const std::string& value) {
  Metric metric(name_hash, MetricType::kRawString);
  metric.string_value = value;
  metrics_.push_back(metric);
}

}  // namespace structured
}  // namespace metrics
