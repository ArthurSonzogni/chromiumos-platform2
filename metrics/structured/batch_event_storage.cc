// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/structured/batch_event_storage.h"

#include <sys/file.h>
#include <time.h>

#include <utility>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include "base/files/file_util.h"
#include <base/rand_util.h>
#include <base/uuid.h>

namespace metrics::structured {

namespace {

constexpr mode_t kFilePermissions = 0660;

// Writes |events| to a file within |directory|. Fails if |directory| doesn't
// exist. Returns whether the write was successful.
bool WriteEventsProtoToDir(const std::string& directory,
                           const EventsProto& events) {
  const std::string guid = base::Uuid::GenerateRandomV4().AsLowercaseString();
  if (guid.empty())
    return false;
  const std::string filepath = base::StrCat({directory, "/", guid});

  ino_t fd_inode = -1;
  ino_t fd_on_disk_inode = -1;
  base::ScopedFD fd;

  // Attempt to lock the file to flush the event. The below loops until an
  // exclusive lock is obtained before Chrome deletes the file.
  do {
    fd.reset(open(filepath.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC,
                  0600));

    if (fd.get() < 0) {
      PLOG(ERROR) << filepath << " cannot open";
      return false;
    }

    // Get inode for the fd.
    struct stat fstat_buf;
    if (fstat(fd.get(), &fstat_buf) < 0) {
      PLOG(ERROR) << "fstat: Could not get " << filepath << " inode";
      return false;
    }
    fd_inode = fstat_buf.st_ino;

    // Grab a lock to avoid chrome from reading an incomplete file.
    if (HANDLE_EINTR(flock(fd.get(), LOCK_EX)) < 0) {
      PLOG(ERROR) << filepath << ": cannot lock for event write.";
      return false;
    }

    // Get inode again for the fd to ensure that the file was not deleted while
    // trying to acquire the lock.
    if (stat(filepath.c_str(), &fstat_buf) < 0) {
      PLOG(ERROR) << "fstat: Could not get " << filepath << " inode";
      // File was deleted by Chrome. Retry the write.
      if (errno == ENOENT) {
        continue;
      }
      return false;
    }

    fd_on_disk_inode = fstat_buf.st_ino;
  } while (fd_inode != fd_on_disk_inode);

  if (!events.SerializeToFileDescriptor(fd.get())) {
    PLOG(ERROR) << filepath << " write error";
    return false;
  }

  // Normally, freeing the FD will unlock the file. However, if the process has
  // been forked, the process may deadlock since flocks are associated to the
  // FD. So, we explicitly unlock the file after the write has completed to
  // avoid deadlocking in the edge case.
  std::ignore = flock(fd.get(), LOCK_UN);

  // Explicitly set permissions on the created event file. This is done
  // separately to the open call to be independent of the umask.
  if (fchmod(fd.get(), kFilePermissions) < 0) {
    PLOG(ERROR) << filepath << " cannot chmod";
    return false;
  }

  return true;
}

}  // namespace

BatchEventStorage::BatchEventStorage(const base::FilePath& events_directory,
                                     BatchEventStorage::StorageParams params)
    : events_directory_(events_directory), params_(params) {
  // Set the last write uptime to time object was created.
  last_write_uptime_ = GetUptime();
}

BatchEventStorage::~BatchEventStorage() {
  Flush();
}

void BatchEventStorage::AddEvent(StructuredEventProto event) {
  events_.mutable_non_uma_events()->Add(std::move(event));
  MaybeWrite();
}

void BatchEventStorage::Purge() {
  events_.Clear();
}

bool BatchEventStorage::IsMaxByteSize() {
  return events_.ByteSizeLong() > params_.max_event_bytes_size;
}

bool BatchEventStorage::IsMaxTimer() {
  base::TimeDelta curr_uptime = GetUptime();

  return curr_uptime - last_write_uptime_ > params_.flush_time_limit;
}

void BatchEventStorage::MaybeWrite() {
  if (IsMaxByteSize()) {
    PLOG(INFO) << "Events at max memory capacity. Triggering a flush.";
    Flush();
  } else if (IsMaxTimer()) {
    PLOG(INFO) << "Events at exceeded timer. Triggering a flush.";
    Flush();
  }
}

void BatchEventStorage::Flush() {
  if (!WriteEventsProtoToDir(events_directory_.value(), events_)) {
    PLOG(WARNING) << "Events flush failed to " << events_directory_.value();
    return;
  }

  Purge();
  last_write_uptime_ = GetUptime();
}

base::TimeDelta BatchEventStorage::GetUptime() {
  if (uptime_for_test_.has_value()) {
    return uptime_for_test_.value();
  }

  timespec boot_time;
  if (clock_gettime(CLOCK_BOOTTIME, &boot_time) != 0) {
    PLOG(FATAL) << "Failed to get boot time.";
  }

  return base::Seconds(boot_time.tv_sec) + base::Nanoseconds(boot_time.tv_nsec);
}

void BatchEventStorage::SetUptimeForTesting(base::TimeDelta curr_uptime,
                                            base::TimeDelta last_write_uptime) {
  uptime_for_test_ = curr_uptime;
  last_write_uptime_ = last_write_uptime;
}

int BatchEventStorage::GetInMemoryEventCountForTesting() const {
  return events_.non_uma_events().size();
}

}  // namespace metrics::structured
