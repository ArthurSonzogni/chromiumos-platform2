// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/anomaly_detector_text_file_reader.h"

#include <stdio.h>

#include <cerrno>
#include <string>
#include <vector>

#include <sys/stat.h>

#include <base/check_op.h>
#include <base/logging.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>

namespace {

const struct VariationsFeature kGravediggerEnabledFeature = {
    .name = "CrOSLateBootGravedigger",
    .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

}  // namespace

namespace anomaly {

TextFileReader::TextFileReader(
    const base::FilePath& path,
    feature::PlatformFeaturesInterface* feature_library)
    : file_path_(path), buf_(kBufferSize_), feature_library_(feature_library) {
  Open();
}

TextFileReader::~TextFileReader() {}

bool TextFileReader::GetLine(std::string* line) {
  if (!HaveOpenLogFile() && !Open())
    return false;

  bool end_of_file = false;
  while (!end_of_file) {
    for (; pos_ < end_pos_; pos_++) {
      if (buf_[pos_] == '\n') {
        if (skip_next_) {
          skip_next_ = false;
          line_fragment_.clear();
          continue;
        }

        pos_++;
        *line = std::string(line_fragment_.begin(), line_fragment_.end());
        line_fragment_.clear();
        return true;
      }

      line_fragment_.push_back(buf_[pos_]);
    }

    end_of_file = true;
    if (LoadToBuffer()) {
      end_of_file = false;
    }
  }

  return false;
}

bool TextFileReader::Open() {
  if (kMaxOpenRetries_ == open_tries_) {
    // Simply return false if the number of retries have reached the limit.
    return false;
  }
  open_tries_++;
  gravedigger_file_.reset();
  direct_file_.Close();

  if (IsGravediggerEnabled()) {
    if (gravedigger::LogFile::PathExists(file_path_)) {
      gravedigger_file_ = gravedigger::LogFile::Open(file_path_);
      LOG_IF(WARNING, !gravedigger_file_)
          << " Try #" << open_tries_
          << " failed to open logfile: " << file_path_.value();
    } else {
      LOG(WARNING) << "Try #" << open_tries_
                   << " no such logfile: " << file_path_.value();
    }
  } else {
    direct_file_ =
        base::File(file_path_, base::File::FLAG_OPEN | base::File::FLAG_READ);

    if (!direct_file_.IsValid()) {
      PLOG(WARNING) << "Try #" << open_tries_
                    << " Failed to open file: " << file_path_.value();
    }
  }
  if (HaveOpenLogFile()) {
    // Reset open_tries_ upon successful Open().
    open_tries_ = 0;
  } else {
    if (kMaxOpenRetries_ == open_tries_) {
      LOG(ERROR) << "Max number of retries to open file " << file_path_.value()
                 << " reached.";
    }
    return false;
  }

  if (gravedigger_file_) {
    inode_number_ = gravedigger_file_->GetInode();
    CHECK_GT(inode_number_, 0);
  } else {
    struct stat st;
    // Use fstat instead of stat to make sure that it gets the inode number for
    // the file that was opened.
    CHECK_GE(fstat(direct_file_.GetPlatformFile(), &st), 0);
    inode_number_ = st.st_ino;
  }
  Clear();
  return true;
}

bool TextFileReader::LoadToBuffer() {
  pos_ = 0;
  end_pos_ = 0;

  int bytes_read = -1;
  if (gravedigger_file_) {
    bytes_read =
        gravedigger_file_->ReadAtCurrentPosition(buf_.data(), buf_.size())
            .value_or(-1);
  } else {
    bytes_read = direct_file_.ReadAtCurrentPos(buf_.data(), buf_.size());
  }
  if (bytes_read > 0) {
    end_pos_ = bytes_read;
    return true;
  }

  // In the unlikely event that Open() fails after CheckForNewFile()
  // returned true, TextFileReader will try to open the file again every time
  // GetLine is called before max number of retries is reached.
  if (CheckForNewFile() && Open()) {
    // rsyslog ensures that a line does not get split between restarts (e.g.
    // during log rotation by chromeos-cleanup-logs) meaning the logs at the end
    // of the original file will be a complete line. Therefore we can safely
    // assume that line_fragment_ is empty and thus can be cleared.
    return LoadToBuffer();
  }

  return false;
}

bool TextFileReader::CheckForNewFile() const {
  struct stat st;

  // TODO(b/329593782): Update file rotation logic once gravedigger handles
  // split files.

  int result = stat(file_path_.value().c_str(), &st);

  // This can happen if a the file_ has been moved but a new file at file_path
  // has not been created yet.
  if (result < 0)
    return false;

  return inode_number_ != st.st_ino;
}

bool TextFileReader::HaveOpenLogFile() const {
  if (gravedigger_file_) {
    return true;
  } else {
    return direct_file_.IsValid();
  }
}

void TextFileReader::SeekToEnd() {
  if (!HaveOpenLogFile())
    return;

  skip_next_ = true;
  Clear();
  if (gravedigger_file_) {
    gravedigger_file_->SeekBeforeEnd();
  } else {
    direct_file_.Seek(base::File::FROM_END, -1);
  }
}

void TextFileReader::SeekToBegin() {
  if (!HaveOpenLogFile())
    return;

  skip_next_ = false;
  Clear();
  if (gravedigger_file_) {
    gravedigger_file_->SeekToBegin();
  } else {
    direct_file_.Seek(base::File::FROM_BEGIN, 0);
  }
}

void TextFileReader::Clear() {
  line_fragment_.clear();
  end_pos_ = 0;
  pos_ = 0;
}

bool TextFileReader::IsGravediggerEnabled() const {
  bool enabled;
  if (feature_library_) {
    enabled = feature_library_->IsEnabledBlocking(kGravediggerEnabledFeature);
  } else {
    enabled = (kGravediggerEnabledFeature.default_state ==
               FeatureState::FEATURE_ENABLED_BY_DEFAULT);
  }
  LOG(INFO) << "Using gravedigger to read log files: "
            << (enabled ? "yes" : "no");
  return enabled;
}

}  // namespace anomaly
