// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/async_file_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <sys/stat.h>

#include "base/logging.h"
#include "power_manager/common/util.h"

namespace {

// Since we don't know the file size in advance, we'll have to read successively
// larger chunks.  Start with 4 KB and double the chunk size with each new read.
const int kInitialFileReadSize = 4096;

// How often to poll for the AIO status.
const int kPollMs = 100;

}  // namespace

namespace power_manager {

AsyncFileReader::AsyncFileReader()
    : read_in_progress_(false),
      fd_(-1),
      aio_buffer_(NULL),
      initial_read_size_(kInitialFileReadSize),
      read_cb_(NULL),
      error_cb_(NULL),
      update_state_timeout_id_(0),
      verbose_(false) {
}

AsyncFileReader::~AsyncFileReader() {
  Reset();
  close(fd_);
}

bool AsyncFileReader::Init(const std::string& filename) {
  CHECK_EQ(fd_, -1) << "Attempting to open new file when a valid file "
                    << "descriptor exists.";
  fd_ = open(filename.c_str(), O_RDONLY, 0);
  if (fd_ == -1) {
    PLOG(ERROR) << "Could not open file " << filename;
    return false;
  }
  filename_ = filename;
  return true;
}

bool AsyncFileReader::HasOpenedFile() const {
  return (fd_ != -1);
}

void AsyncFileReader::StartRead(
    base::Callback<void(const std::string&)>* read_cb,
    base::Callback<void()>* error_cb) {
  LOG_IF(INFO, verbose_) << "Starting read of " << filename_;
  Reset();

  if (fd_ == -1) {
    LOG(ERROR) << "No file handle available.";
    if (error_cb)
      error_cb->Run();
    return;
  }

  if (!AsyncRead(initial_read_size_, 0)) {
    if (error_cb)
      error_cb->Run();
    return;
  }
  read_cb_ = read_cb;
  error_cb_ = error_cb;
  read_in_progress_ = true;
}

gboolean AsyncFileReader::UpdateState() {
  LOG_IF(INFO, verbose_)
      << "Updating state; read_in_progress=" << read_in_progress_;
  if (!read_in_progress_) {
    update_state_timeout_id_ = 0;
    return FALSE;
  }

  int status = aio_error(&aio_control_);
  LOG_IF(INFO, verbose_) << "Status is " << status;

  // If the read is still in-progress, keep the timeout alive.
  if (status == EINPROGRESS)
    return TRUE;

  // Otherwise, we'll return false later to cancel the timeout.  Reset its
  // ID first to make sure that none of the calls to Reset() delete it.
  update_state_timeout_id_ = 0;

  switch (status) {
    case ECANCELED:
      Reset();
      break;
    case 0: {
      size_t size = aio_return(&aio_control_);
      // Save the data that was read, and free the buffer.
      stored_data_.insert(
          stored_data_.end(), aio_buffer_, aio_buffer_ + size);
      delete [] aio_buffer_;
      aio_buffer_ = NULL;

      if (size == aio_control_.aio_nbytes) {
        // Read more data if the previous read didn't reach the end of file.
        if (AsyncRead(size * 2, aio_control_.aio_offset + size))
          break;
      }
      if (read_cb_)
        read_cb_->Run(stored_data_);
      Reset();
      break;
    }
    default: {
      LOG(ERROR) << "Error during read of file " << filename_
                 << ", status=" << status;
      if (error_cb_)
        error_cb_->Run();
      Reset();
      break;
    }
  }

  return FALSE;
}

void AsyncFileReader::Reset() {
  LOG_IF(INFO, verbose_ && read_in_progress_) << "Resetting state";
  if (!read_in_progress_)
    return;

  util::RemoveTimeout(&update_state_timeout_id_);

  // TODO(derat): Determine if unhandled AIO_NOTCANCELED results are the
  // cause of http://crosbug.com/38732.
  int cancel_result = aio_cancel(fd_, &aio_control_);
  if (cancel_result == -1)
    PLOG(ERROR) << "aio_cancel() failed";
  else if (cancel_result == AIO_NOTCANCELED)
    LOG(ERROR) << "aio_cancel() returned AIO_NOTCANCELED";

  delete [] aio_buffer_;
  aio_buffer_ = NULL;
  stored_data_.clear();
  read_cb_ = NULL;
  error_cb_ = NULL;
  read_in_progress_ = false;
}

bool AsyncFileReader::AsyncRead(int size, int offset) {
  aio_buffer_ = new char[size];

  memset(&aio_control_, 0, sizeof(aio_control_));
  aio_control_.aio_nbytes = size;
  aio_control_.aio_fildes = fd_;
  aio_control_.aio_offset = offset;
  aio_control_.aio_buf = aio_buffer_;

  if (aio_read(&aio_control_) == -1) {
    LOG(ERROR) << "Unable to access " << filename_;
    delete [] aio_buffer_;
    aio_buffer_ = NULL;
    return false;
  }

  DCHECK_EQ(update_state_timeout_id_, static_cast<guint>(0));
  update_state_timeout_id_ = g_timeout_add(kPollMs, UpdateStateThunk, this);
  return true;
}

}  // namespace power_manager
