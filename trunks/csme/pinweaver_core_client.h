// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_CSME_PINWEAVER_CORE_CLIENT_H_
#define TRUNKS_CSME_PINWEAVER_CORE_CLIENT_H_

#include <memory>
#include <string>

#include "trunks/csme/mei_client.h"
#include "trunks/csme/mei_client_factory.h"
#include "trunks/csme/pinweaver_csme_types.h"

namespace trunks {
namespace csme {

class PinWeaverCoreClient {
 public:
  explicit PinWeaverCoreClient(MeiClientFactory* mei_client_factory);
  bool PinWeaverCommand(const std::string& pinweaver_request,
                        std::string* pinweaver_response);

 private:
  MeiClient* GetMeiClient();
  bool UnpackFromResponse(const pw_heci_header_req& req_header,
                          const std::string& response,
                          std::string* payload);

  MeiClientFactory* const mei_client_factory_;
  std::unique_ptr<MeiClient> mei_client_;
  int seq_ = 0;
};

}  // namespace csme
}  // namespace trunks

#endif  // TRUNKS_CSME_PINWEAVER_CORE_CLIENT_H_
