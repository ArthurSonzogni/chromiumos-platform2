// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/persistent_integer.h"

#include <fcntl.h>

#include <algorithm>

#include <base/check.h>
#include <base/containers/span.h>
#include <base/files/file.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_util.h>

#include "metrics/metrics_library.h"

namespace chromeos_metrics {

namespace {

// Returns a pointer to the creation callback. The normal "Use a 'leaked'
// function-level static object to avoid global initialization & destruction
// issues" trick.
base::RepeatingCallback<void(const base::FilePath&)>* GetCreationCallback() {
  static base::RepeatingCallback<void(const base::FilePath&)>* p =
      new base::RepeatingCallback<void(const base::FilePath&)>;

  return p;
}

}  // namespace

// Static class member instantiation.
bool PersistentInteger::testing_ = false;

PersistentInteger::PersistentInteger(const base::FilePath& backing_file_path)
    : path_(backing_file_path), synced_(false), value_(0), version_(kVersion) {
  if (!GetCreationCallback()->is_null()) {
    GetCreationCallback()->Run(backing_file_path);
  }
}

PersistentInteger::~PersistentInteger() {}

void PersistentInteger::Set(int64_t value) {
  value_ = value;
  Write();
}

int64_t PersistentInteger::Get() {
  // If not synced, then read.  If the read fails, it's a good idea to write.
  // The write will create the file if needed.
  if (!synced_ && !Read()) {
    Write();
  }
  return value_;
}

int64_t PersistentInteger::GetAndClear() {
  int64_t v = Get();
  Set(0);
  return v;
}

void PersistentInteger::Add(int64_t x) {
  Set(Get() + x);
}

void PersistentInteger::Max(int64_t x) {
  Set(std::max(Get(), x));
}

void PersistentInteger::Write() {
  // Open the backing file, creating it if it doesn't exist.
  base::File f(path_, base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!f.IsValid()) {
    // The disk might be bad. Not much we (or the caller) can do; just log an
    // ERROR. (That might fail, too, if the disk isn't writeable)
    PLOG(ERROR) << "cannot open " << path_.MaybeAsASCII();
    return;
  }

  if (!(f.WriteAndCheck(0, base::byte_span_from_ref(version_)) &&
        f.WriteAndCheck(sizeof(version_), base::byte_span_from_ref(value_)))) {
    PLOG(ERROR) << "cannot write to " << path_.MaybeAsASCII();
    return;
  }
  synced_ = true;
}

bool PersistentInteger::Read() {
  base::File f(path_, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!f.IsValid()) {
    return false;
  }
  int32_t version;
  int64_t value;
  if (!f.ReadAndCheck(0, base::byte_span_from_ref(version)) ||
      version != version_ ||
      !f.ReadAndCheck(sizeof(version), base::byte_span_from_ref(value))) {
    return false;
  }
  value_ = value;
  synced_ = true;
  return true;
}

// static
void PersistentInteger::SetCreationCallbackForTesting(
    const base::RepeatingCallback<void(const base::FilePath&)>&
        creation_callback) {
  *GetCreationCallback() = creation_callback;
}

// static
void PersistentInteger::ClearCreationCallbackForTesting() {
  GetCreationCallback()->Reset();
}

}  // namespace chromeos_metrics
