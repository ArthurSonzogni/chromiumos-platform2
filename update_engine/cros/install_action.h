// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_INSTALL_ACTION_H_
#define UPDATE_ENGINE_CROS_INSTALL_ACTION_H_

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <crypto/secure_hash.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <libimageloader/manifest.h>

#include "update_engine/common/action.h"
#include "update_engine/common/http_fetcher.h"
#include "update_engine/cros/image_properties.h"

// The Installation action flow for scaled DLC(s).

namespace chromeos_update_engine {

class NoneType;
class InstallAction;
class OmahaRequestParams;

extern const char kDefaultSlotting[];
extern const char kForceOTASlotting[];

template <>
class ActionTraits<InstallAction> {
 public:
  // No input/output objects.
  typedef NoneType InputObjectType;
  typedef NoneType OutputObjectType;
};

class InstallActionDelegate {
 public:
  virtual ~InstallActionDelegate() = default;

  // Called periodically after bytes are received.
  // `bytes_received` is the total number of bytes installed.
  // `total` is the target bytes to install.
  virtual void BytesReceived(uint64_t bytes_received, uint64_t total) = 0;
};

class InstallAction : public Action<InstallAction>, public HttpFetcherDelegate {
 public:
  // Args:
  //  http_fetcher: An HttpFetcher to take ownership of. Injected for testing.
  //  id: The DLC ID to install.
  //  slotting: Override of scaled DLC slotting to use, empty to use default.
  InstallAction(std::unique_ptr<HttpFetcher> http_fetcher,
                const std::string& id,
                const std::string& slotting = "",
                const std::string& manifest_dir = "");
  InstallAction(const InstallAction&) = delete;
  InstallAction& operator=(const InstallAction&) = delete;

  ~InstallAction() override;
  typedef ActionTraits<InstallAction>::InputObjectType InputObjectType;
  typedef ActionTraits<InstallAction>::OutputObjectType OutputObjectType;
  void PerformAction() override;
  void TerminateProcessing() override;

  int GetHTTPResponseCode() { return http_fetcher_->http_response_code(); }

  // Debugging/logging
  static std::string StaticType() { return "InstallAction"; }
  std::string Type() const override { return StaticType(); }

  // Delegate methods (see http_fetcher.h)
  bool ReceivedBytes(HttpFetcher* fetcher,
                     const void* bytes,
                     size_t length) override;
  void TransferComplete(HttpFetcher* fetcher, bool successful) override;
  void TransferTerminated(HttpFetcher* fetcher) override;

  InstallActionDelegate* delegate() const { return delegate_; }
  void set_delegate(InstallActionDelegate* delegate) { delegate_ = delegate; }

 private:
  FRIEND_TEST(InstallActionTestSuite, TransferFailureFetchesFromBackup);

  void StartInstallation(const std::string& url_to_fetch);

  void TerminateInstallation();

  InstallActionDelegate* delegate_{nullptr};

  // Hasher to hash as artifacts get fetched.
  std::unique_ptr<crypto::SecureHash> hash_;

  ImageProperties image_props_;

  // The HTTP fetcher given ownership to.
  std::unique_ptr<HttpFetcher> http_fetcher_;

  // The DLC ID.
  std::string id_;

  // The Lorry slotting to use for fetches.
  std::string slotting_;

  // Offset into `f_` that are being written to, it's faster to cache instead of
  // lseek'ing on the offset.
  int64_t offset_{0};
  base::File f_;

  // The list of backup URLs.
  std::vector<std::string> backup_urls_;
  int backup_url_index_{0};

  // The DLC manifest accessor.
  std::shared_ptr<imageloader::Manifest> manifest_;
  std::string manifest_dir_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_INSTALL_ACTION_H_
