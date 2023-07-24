// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_requests_cache.h"

#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace dlp {

namespace {

IsFilesTransferRestrictedRequest BuildRequest(
    const std::vector<std::pair<FileId, std::string>>& files,
    const std::string& destination_url,
    DlpComponent destination_component) {
  IsFilesTransferRestrictedRequest request;
  request.set_destination_url(destination_url);
  request.set_destination_component(destination_component);
  for (const auto& [id, path] : files) {
    FileMetadata* file_metadata = request.add_transferred_files();
    file_metadata->set_inode(id.first);
    file_metadata->set_crtime(id.second);
    file_metadata->set_path(path);
  }
  return request;
}

IsFilesTransferRestrictedResponse BuildResponse(
    IsFilesTransferRestrictedRequest request,
    const std::vector<RestrictionLevel>& levels) {
  IsFilesTransferRestrictedResponse response;
  int i = 0;
  for (const auto& file : request.transferred_files()) {
    FileRestriction* file_restriction = response.add_files_restrictions();
    *file_restriction->mutable_file_metadata() = file;
    file_restriction->set_restriction_level(levels[i++]);
  }
  return response;
}

}  // namespace

class DlpRequestsCacheTest : public ::testing::Test {
 public:
  DlpRequestsCacheTest() = default;
  ~DlpRequestsCacheTest() override = default;

  DlpRequestsCacheTest(const DlpRequestsCacheTest&) = delete;
  DlpRequestsCacheTest& operator=(const DlpRequestsCacheTest&) = delete;

 protected:
  DlpRequestsCache requests_cache_;
};

TEST_F(DlpRequestsCacheTest, EmptyCache) {
  EXPECT_EQ(RestrictionLevel::LEVEL_UNSPECIFIED,
            requests_cache_.Get({/*inode=*/1, /*crtime=*/0}, "path",
                                "destination", UNKNOWN_COMPONENT));
}

TEST_F(DlpRequestsCacheTest, CacheResult) {
  IsFilesTransferRestrictedRequest request =
      BuildRequest({{{/*inode=*/1, /*crtime=*/0}, "path"},
                    {{/*inode=*/2, /*crtime=*/0}, "path2"}},
                   "destination", UNKNOWN_COMPONENT);
  IsFilesTransferRestrictedResponse response = BuildResponse(
      request, {RestrictionLevel::LEVEL_ALLOW, RestrictionLevel::LEVEL_BLOCK});
  requests_cache_.CacheResult(request, response);
  EXPECT_EQ(RestrictionLevel::LEVEL_ALLOW,
            requests_cache_.Get({/*inode=*/1, /*crtime=*/0}, "path",
                                "destination", UNKNOWN_COMPONENT));
  EXPECT_EQ(RestrictionLevel::LEVEL_BLOCK,
            requests_cache_.Get({/*inode=*/2, /*crtime=*/0}, "path2",
                                "destination", UNKNOWN_COMPONENT));
  EXPECT_EQ(RestrictionLevel::LEVEL_UNSPECIFIED,
            requests_cache_.Get({/*inode=*/2, /*crtime=*/0}, "path",
                                "destination", UNKNOWN_COMPONENT));
}

TEST_F(DlpRequestsCacheTest, ResetCache) {
  IsFilesTransferRestrictedRequest request =
      BuildRequest({{{/*inode=*/1, /*crtime=*/0}, "path"}}, "destination",
                   UNKNOWN_COMPONENT);
  IsFilesTransferRestrictedResponse response =
      BuildResponse(request, {RestrictionLevel::LEVEL_ALLOW});
  requests_cache_.CacheResult(request, response);
  EXPECT_EQ(RestrictionLevel::LEVEL_ALLOW,
            requests_cache_.Get({/*inode=*/1, /*crtime=*/0}, "path",
                                "destination", UNKNOWN_COMPONENT));
  requests_cache_.ResetCache();
  EXPECT_EQ(RestrictionLevel::LEVEL_UNSPECIFIED,
            requests_cache_.Get({/*inode=*/1, /*crtime=*/0}, "path",
                                "destination", UNKNOWN_COMPONENT));
}

TEST_F(DlpRequestsCacheTest, ValueOverridden) {
  IsFilesTransferRestrictedRequest request =
      BuildRequest({{{/*inode=*/1, /*crtime=*/0}, "path"}}, "destination",
                   UNKNOWN_COMPONENT);
  IsFilesTransferRestrictedResponse response_1 =
      BuildResponse(request, {RestrictionLevel::LEVEL_ALLOW});
  requests_cache_.CacheResult(request, response_1);
  EXPECT_EQ(RestrictionLevel::LEVEL_ALLOW,
            requests_cache_.Get({/*inode=*/1, /*crtime=*/0}, "path",
                                "destination", UNKNOWN_COMPONENT));
  IsFilesTransferRestrictedResponse response_2 =
      BuildResponse(request, {RestrictionLevel::LEVEL_WARN_CANCEL});
  requests_cache_.CacheResult(request, response_2);
  EXPECT_EQ(RestrictionLevel::LEVEL_WARN_CANCEL,
            requests_cache_.Get({/*inode=*/1, /*crtime=*/0}, "path",
                                "destination", UNKNOWN_COMPONENT));
}

}  // namespace dlp
