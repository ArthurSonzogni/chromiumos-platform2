// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TIMBERSLIDE_TIMBERSLIDE_H_
#define TIMBERSLIDE_TIMBERSLIDE_H_

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/file_path_watcher.h>
#include <base/time/time.h>
#include <brillo/daemons/daemon.h>
#include "timberslide/log_listener.h"
#include "timberslide/string_transformer.h"

#include "pw_tokenizer/detokenize.h"
#include "pw_tokenizer/token_database.h"

namespace timberslide {

class TimberSlide : public brillo::Daemon {
 public:
  TimberSlide(const std::string& ec_type,
              base::File device_file,
              base::File uptime_file,
              const base::FilePath& log_dir,
              const base::FilePath& token_db);

  std::string ProcessLogBuffer(const std::string& buffer,
                               const base::Time& now);

 protected:
  // For testing
  explicit TimberSlide(std::unique_ptr<LogListener> log_listener,
                       std::unique_ptr<StringTransformer> xfrm);

 private:
  int OnInit() override;

  void OnEventReadable();
  void OnEventTokenChange(const base::FilePath& file_path, bool error);

  virtual bool GetEcUptime(int64_t* ec_uptime_ms);

  void RotateLogs(const base::FilePath& previous_log,
                  const base::FilePath& current_log);

  pw::tokenizer::Detokenizer OpenDatabase(const base::FilePath& token_db);

  base::File device_file_;
  base::FilePath current_log_;
  base::FilePath previous_log_;
  base::FilePath tokens_db_;

  pw::tokenizer::Detokenizer detokenizer_;

  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  std::unique_ptr<base::FilePathWatcher> token_watcher_;
  int total_size_ = 0;
  base::File uptime_file_;
  bool uptime_file_valid_ = false;
  bool tokenized_logging_ = false;
  std::unique_ptr<LogListener> log_listener_;
  std::unique_ptr<StringTransformer> xfrm_;
};

}  // namespace timberslide

#endif  // TIMBERSLIDE_TIMBERSLIDE_H_
