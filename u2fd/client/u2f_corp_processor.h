// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_CLIENT_U2F_CORP_PROCESSOR_H_
#define U2FD_CLIENT_U2F_CORP_PROCESSOR_H_

#include "u2fd/client/u2f_apdu.h"

namespace u2f {

class U2fCorpProcessor {
 public:
  U2fCorpProcessor() = default;
  virtual ~U2fCorpProcessor() = default;

  virtual void Initialize() = 0;

  virtual U2fResponseApdu ProcessApdu(const U2fCommandApdu& apdu) = 0;
};

}  // namespace u2f

#endif  // U2FD_CLIENT_U2F_CORP_PROCESSOR_H_
