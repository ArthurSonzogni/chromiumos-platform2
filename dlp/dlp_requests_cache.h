// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLP_DLP_REQUESTS_CACHE_H_
#define DLP_DLP_REQUESTS_CACHE_H_

#include <stdint.h>

#include <map>
#include <string>
#include <sys/types.h>
#include <vector>

#include "dlp/proto_bindings/dlp_service.pb.h"

namespace dlp {

typedef ino64_t FileId;

// In-memory cache of results of IsFilesTransferRestricted evaluation done in
// Chrome.
class DlpRequestsCache {
 public:
  DlpRequestsCache();

  // Not copyable or movable.
  DlpRequestsCache(const DlpRequestsCache&) = delete;
  DlpRequestsCache& operator=(const DlpRequestsCache&) = delete;

  ~DlpRequestsCache();

  // Cache the resulting |response| to the |request|.
  void CacheResult(IsFilesTransferRestrictedRequest request,
                   IsFilesTransferRestrictedResponse response);

  // Return cached restriction level for a single file request, is available.
  // Return LEVEL_NOT_SPECIFIED by default.
  RestrictionLevel Get(FileId id,
                       const std::string& path,
                       const std::string& destination_url,
                       DlpComponent destination_component) const;

  // Removes all the entries.
  void ResetCache();

 private:
  // Internal comparable structure to store in the map.
  struct CachedRequest {
    CachedRequest(FileId id,
                  const std::string& path,
                  const std::string& destination_url,
                  DlpComponent destination_component);
    bool operator<(const CachedRequest& o) const;

    FileId id;
    std::string path;
    std::string destination_url;
    DlpComponent destination_component;
  };

  // Caching a single file evaluation result.
  void CacheFileRequest(FileId id,
                        const std::string& path,
                        const std::string& destination_url,
                        DlpComponent destination_component,
                        RestrictionLevel restriction_level);

  // Map to store the cache.
  std::map<CachedRequest, RestrictionLevel> cached_requests_;
};

}  // namespace dlp

#endif  // DLP_DLP_REQUESTS_CACHE_H_
