// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/structured/persistent_proto.h"

#include <sys/file.h>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include <base/logging.h>
#include <base/rand_util.h>

#include "metrics/structured/proto/storage.pb.h"

#define READ_WRITE_ALL_FILE_FLAGS \
  (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

namespace metrics {
namespace structured {
namespace {

template <class T>
std::unique_ptr<T> ReadFile(const base::FilePath& filepath) {
  if (!base::PathExists(filepath))
    return nullptr;

  std::string proto_str;
  if (!base::ReadFileToString(filepath, &proto_str))
    return nullptr;

  auto proto = std::make_unique<T>();
  if (!proto->ParseFromString(proto_str))
    return nullptr;

  return std::move(proto);
}

template <class T>
bool WriteFile(const std::string& filepath, const T* proto) {
  std::string proto_str;
  if (!proto->SerializeToString(&proto_str))
    return false;

  base::ScopedFD file_descriptor(open(filepath.c_str(),
                                      O_WRONLY | O_APPEND | O_CREAT,
                                      READ_WRITE_ALL_FILE_FLAGS));
  if (file_descriptor.get() < 0) {
    PLOG(ERROR) << filepath << " cannot open";
    return false;
  }

  // Grab a lock to avoid chrome deleting the file while we're writing. Keep the
  // file locked as briefly as possible. Freeing file_descriptor will close the
  // file and remove the lock.
  if (HANDLE_EINTR(flock(file_descriptor.get(), LOCK_EX)) < 0) {
    PLOG(ERROR) << filepath << " cannot lock";
    return false;
  }

  if (!base::WriteFileDescriptor(file_descriptor.get(), proto_str)) {
    PLOG(ERROR) << filepath << " write error";
    return false;
  }

  return true;
}

}  // namespace

template <class T>
PersistentProto<T>::PersistentProto(const std::string& path) : path_(path) {
  auto file = ReadFile<T>(base::FilePath(path));
  if (file != nullptr) {
    proto_ = std::move(file);
  } else {
    proto_ = std::make_unique<T>();
    Write();
  }
}

template <class T>
PersistentProto<T>::~PersistentProto() = default;

template <class T>
void PersistentProto<T>::Write() {
  WriteFile<T>(path_, proto_.get());
}

// A list of all types that the PersistentProto can be used with.
template class PersistentProto<EventsProto>;
template class PersistentProto<KeyDataProto>;
template class PersistentProto<KeyProto>;

}  // namespace structured
}  // namespace metrics
