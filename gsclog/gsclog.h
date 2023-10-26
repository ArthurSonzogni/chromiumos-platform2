// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GSCLOG_GSCLOG_H_
#define GSCLOG_GSCLOG_H_

#include <memory>

#include <base/files/file_util.h>
#include <trunks/trunks_factory.h>
#include <trunks/trunks_factory_impl.h>

namespace gsclog {

class GscLog {
 public:
  explicit GscLog(const base::FilePath& log_dir);

  int Fetch();

 private:
  base::FilePath log_;
  std::unique_ptr<trunks::TrunksFactoryImpl> default_trunks_factory_;
  trunks::TrunksFactory* trunks_factory_{nullptr};
  std::unique_ptr<trunks::TpmUtility> trunks_utility_;
};

}  // namespace gsclog

#endif  // GSCLOG_GSCLOG_H_
