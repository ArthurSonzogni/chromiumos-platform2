// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dns/io_buffer.h"

#include "base/logging.h"
#include "base/numerics/safe_math.h"

#include <base/check_op.h>

namespace patchpanel {

namespace {

void AssertValidBufferSize(size_t size) {
  base::CheckedNumeric<int>(size).ValueOrDie();
}

}  // namespace

IOBuffer::IOBuffer() : data_(nullptr) {}

IOBuffer::IOBuffer(size_t buffer_size) {
  AssertValidBufferSize(buffer_size);
  data_ = new char[buffer_size];
}

IOBuffer::IOBuffer(char* data) : data_(data) {}

IOBuffer::~IOBuffer() {
  delete[] data_;
  data_ = nullptr;
}

IOBufferWithSize::IOBufferWithSize(size_t size) : IOBuffer(size), size_(size) {
  // Note: Size check is done in superclass' constructor. This will check if
  // |size| was larger than INT_MAX.
}

IOBufferWithSize::IOBufferWithSize(char* data, size_t size)
    : IOBuffer(data), size_(size) {
  AssertValidBufferSize(size);
}

IOBufferWithSize::~IOBufferWithSize() = default;

StringIOBuffer::StringIOBuffer(const std::string& s)
    : IOBuffer(nullptr), string_data_(s) {
  AssertValidBufferSize(s.size());
  data_ = const_cast<char*>(string_data_.data());
}

StringIOBuffer::StringIOBuffer(std::unique_ptr<std::string> s)
    : IOBuffer(nullptr) {
  AssertValidBufferSize(s->size());
  string_data_.swap(*s.get());
  data_ = const_cast<char*>(string_data_.data());
}

StringIOBuffer::~StringIOBuffer() {
  // We haven't allocated the buffer, so remove it before the base class
  // destructor tries to delete[] it.
  data_ = nullptr;
}

DrainableIOBuffer::DrainableIOBuffer(IOBuffer* base, size_t size)
    : IOBuffer(base->data()), base_(base), size_(size), used_(0) {
  AssertValidBufferSize(size);
}

void DrainableIOBuffer::DidConsume(size_t bytes) {
  SetOffset(used_ + bytes);
}

size_t DrainableIOBuffer::BytesRemaining() const {
  return size_ - used_;
}

// Returns the number of consumed bytes.
size_t DrainableIOBuffer::BytesConsumed() const {
  return used_;
}

void DrainableIOBuffer::SetOffset(size_t bytes) {
  DCHECK_GE(bytes, 0);
  DCHECK_LE(bytes, size_);
  used_ = bytes;
  data_ = base_->data() + used_;
}

DrainableIOBuffer::~DrainableIOBuffer() {
  // The buffer is owned by the |base_| instance.
  data_ = nullptr;
}

GrowableIOBuffer::GrowableIOBuffer() : IOBuffer(), capacity_(0), offset_(0) {}

void GrowableIOBuffer::SetCapacity(size_t capacity) {
  DCHECK_GE(capacity, 0);
  // realloc will crash if it fails.
  real_data_.reset(static_cast<char*>(realloc(real_data_.release(), capacity)));
  capacity_ = capacity;
  if (offset_ > capacity)
    set_offset(capacity);
  else
    set_offset(offset_);  // The pointer may have changed.
}

void GrowableIOBuffer::set_offset(size_t offset) {
  DCHECK_GE(offset, 0);
  DCHECK_LE(offset, capacity_);
  offset_ = offset;
  data_ = real_data_.get() + offset;
}

size_t GrowableIOBuffer::RemainingCapacity() {
  return capacity_ - offset_;
}

char* GrowableIOBuffer::StartOfBuffer() {
  return real_data_.get();
}

GrowableIOBuffer::~GrowableIOBuffer() {
  data_ = nullptr;
}

PickledIOBuffer::PickledIOBuffer() = default;

void PickledIOBuffer::Done() {
  data_ = reinterpret_cast<char*>(const_cast<uint8_t*>(pickle_.data()));
}

PickledIOBuffer::~PickledIOBuffer() {
  data_ = nullptr;
}

WrappedIOBuffer::WrappedIOBuffer(const char* data)
    : IOBuffer(const_cast<char*>(data)) {}

WrappedIOBuffer::~WrappedIOBuffer() {
  data_ = nullptr;
}

}  // namespace patchpanel
