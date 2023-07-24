// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_requests_cache.h"

namespace dlp {

DlpRequestsCache::DlpRequestsCache() {}
DlpRequestsCache::~DlpRequestsCache() = default;

void DlpRequestsCache::CacheResult(IsFilesTransferRestrictedRequest request,
                                   IsFilesTransferRestrictedResponse response) {
  for (const auto& file : *response.mutable_files_restrictions()) {
    CacheFileRequest(
        {file.file_metadata().inode(), file.file_metadata().crtime()},
        file.file_metadata().path(), request.destination_url(),
        request.destination_component(), file.restriction_level());
  }
}

RestrictionLevel DlpRequestsCache::Get(
    FileId id,
    const std::string& path,
    const std::string& destination_url,
    DlpComponent destination_component) const {
  CachedRequest cached_file(id, path, destination_url, destination_component);
  auto it = cached_requests_.find(cached_file);
  if (it == cached_requests_.end()) {
    return LEVEL_UNSPECIFIED;
  }
  return it->second;
}

void DlpRequestsCache::ResetCache() {
  cached_requests_.clear();
}

DlpRequestsCache::CachedRequest::CachedRequest(
    FileId id,
    const std::string& path,
    const std::string& destination_url,
    DlpComponent destination_component)
    : id(id),
      path(path),
      destination_url(destination_url),
      destination_component(destination_component) {}

bool DlpRequestsCache::CachedRequest::operator<(const CachedRequest& o) const {
  if (id != o.id) {
    return id < o.id;
  }
  if (path != o.path) {
    return path < o.path;
  }
  if (destination_url != o.destination_url) {
    return destination_url < o.destination_url;
  }
  return destination_component < o.destination_component;
}

void DlpRequestsCache::CacheFileRequest(FileId id,
                                        const std::string& path,
                                        const std::string& destination_url,
                                        DlpComponent destination_component,
                                        RestrictionLevel restriction_level) {
  CachedRequest cached_file_request(id, path, destination_url,
                                    destination_component);
  cached_requests_.insert_or_assign(cached_file_request, restriction_level);
}

};  // namespace dlp
