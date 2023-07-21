// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"

#include <base/logging.h>
#include <brillo/syslog_logging.h>

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog);

  flex_hwis::FlexHwisSender flex_hwis_sender(base::FilePath("/"));
  auto flex_hwis_res = flex_hwis_sender.CollectAndSend();

  switch (flex_hwis_res) {
    case flex_hwis::Result::Sent:
      LOG(INFO) << "flex_hwis_tool ran successfully";
      break;
    case flex_hwis::Result::HasRunRecently:
      LOG(INFO) << "flex_hwis_tool cannot be run within 24 hour";
      break;
    case flex_hwis::Result::NotAuthorized:
      LOG(INFO) << "flex_hwis_tool wasn't authorized to send data";
      break;
    default:
      LOG(INFO) << "flex_hwis_tool has unexpected return value";
  }
  return 0;
}
