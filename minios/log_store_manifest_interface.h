// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_LOG_STORE_MANIFEST_INTERFACE_H_
#define MINIOS_LOG_STORE_MANIFEST_INTERFACE_H_

#include <minios/proto_bindings/minios.pb.h>

namespace minios {

// Interface for a log store manifest helper class.
class LogStoreManifestInterface {
 public:
  virtual ~LogStoreManifestInterface() = default;

  // Generate a manifest with the given `entry`.
  virtual bool Generate(const LogManifest::Entry& entry) = 0;

  // Retrieve a previously written manifest from disk. This is done by
  // inspecting the first `sizeof(kLogStoreMagic)` bytes of every block on
  // `disk_path` until a magic value is found. If no manifest is found on disk,
  // a `nullopt` is returned.
  virtual std::optional<LogManifest> Retreive() = 0;

  // Write a manifest in the `manifest_store_offset_block` of the current disk.
  // Note that the first `sizeof(kLogStoreMagic)` bytes will be a magic value,
  // followed by the serialized protobuf.
  virtual bool Write() = 0;

  // Clear any manifest stores found on disk. Similar to `Retrieve` we first
  // seek the manifest store, and then write `0` until the end of the partition.
  virtual void Clear() = 0;
};

}  // namespace minios

#endif  // MINIOS_LOG_STORE_MANIFEST_INTERFACE_H_
