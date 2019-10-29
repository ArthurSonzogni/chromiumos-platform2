// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/util.h"

#include <poll.h>
#include <signal.h>
#include <stdlib.h>

#include <map>
#include <memory>

#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/cryptohome.h>
#include <brillo/key_value_store.h>
#include <vboot/crossystem.h>
#include <zlib.h>

#include "crash-reporter/paths.h"

namespace util {

namespace {

constexpr size_t kBufferSize = 4096;

// Path to hardware class description.
constexpr char kHwClassPath[] = "/sys/devices/platform/chromeos_acpi/HWID";

constexpr char kDevSwBoot[] = "devsw_boot";
constexpr char kDevMode[] = "dev";

// If the OS version is older than this we do not upload crash reports.
constexpr base::TimeDelta kAgeForNoUploads = base::TimeDelta::FromDays(180);

}  // namespace

const int kDefaultMaxUploadBytes = 1024 * 1024;

bool IsCrashTestInProgress() {
  return base::PathExists(paths::GetAt(paths::kSystemRunStateDirectory,
                                       paths::kCrashTestInProgress));
}

bool IsDeviceCoredumpUploadAllowed() {
  return base::PathExists(paths::GetAt(paths::kCrashReporterStateDirectory,
                                       paths::kDeviceCoredumpUploadAllowed));
}

bool IsDeveloperImage() {
  // If we're testing crash reporter itself, we don't want to special-case
  // for developer images.
  if (IsCrashTestInProgress())
    return false;
  return base::PathExists(paths::Get(paths::kLeaveCoreFile));
}

bool IsTestImage() {
  // If we're testing crash reporter itself, we don't want to special-case
  // for test images.
  if (IsCrashTestInProgress())
    return false;

  std::string channel;
  if (!GetCachedKeyValueDefault(base::FilePath(paths::kLsbRelease),
                                "CHROMEOS_RELEASE_TRACK", &channel)) {
    return false;
  }
  return base::StartsWith(channel, "test", base::CompareCase::SENSITIVE);
}

bool IsOfficialImage() {
  std::string description;
  if (!GetCachedKeyValueDefault(base::FilePath(paths::kLsbRelease),
                                "CHROMEOS_RELEASE_DESCRIPTION", &description)) {
    return false;
  }

  return description.find("Official") != std::string::npos;
}

base::Time GetOsTimestamp() {
  base::FilePath lsb_release_path =
      paths::Get(paths::kEtcDirectory).Append(paths::kLsbRelease);
  base::File::Info info;
  if (!base::GetFileInfo(lsb_release_path, &info)) {
    LOG(ERROR) << "Failed reading info for /etc/lsb-release";
    return base::Time();
  }

  return info.last_modified;
}

bool IsOsTimestampTooOldForUploads(base::Time timestamp, base::Clock* clock) {
  return !timestamp.is_null() && (clock->Now() - timestamp) > kAgeForNoUploads;
}

std::string GetHardwareClass() {
  std::string hw_class;
  if (base::ReadFileToString(paths::Get(kHwClassPath), &hw_class))
    return hw_class;
  char hw_class_arr[VB_MAX_STRING_PROPERTY];
  if (!VbGetSystemPropertyString("hwid", hw_class_arr, sizeof(hw_class_arr)))
    return "undefined";
  return hw_class_arr;
}

std::string GetBootModeString() {
  // If we're testing crash reporter itself, we don't want to special-case
  // for developer mode.
  if (IsCrashTestInProgress())
    return "";

  int vb_value = VbGetSystemPropertyInt(kDevSwBoot);
  if (vb_value < 0) {
    LOG(ERROR) << "Error trying to determine boot mode";
    return "missing-crossystem";
  }
  if (vb_value == 1)
    return kDevMode;

  return "";
}

bool GetCachedKeyValue(const base::FilePath& base_name,
                       const std::string& key,
                       const std::vector<base::FilePath>& directories,
                       std::string* value) {
  std::vector<std::string> error_reasons;
  for (const auto& directory : directories) {
    const base::FilePath file_name = directory.Append(base_name);
    if (!base::PathExists(file_name)) {
      error_reasons.push_back(file_name.value() + " not found");
      continue;
    }
    brillo::KeyValueStore store;
    if (!store.Load(file_name)) {
      LOG(WARNING) << "Problem parsing " << file_name.value();
      // Even though there was some failure, take as much as we could read.
    }
    if (!store.GetString(key, value)) {
      error_reasons.push_back("Key not found in " + file_name.value());
      continue;
    }
    return true;
  }
  LOG(WARNING) << "Unable to find " << key << ": "
               << base::JoinString(error_reasons, ", ");
  return false;
}

bool GetCachedKeyValueDefault(const base::FilePath& base_name,
                              const std::string& key,
                              std::string* value) {
  const std::vector<base::FilePath> kDirectories = {
      paths::Get(paths::kCrashReporterStateDirectory),
      paths::Get(paths::kEtcDirectory),
  };
  return GetCachedKeyValue(base_name, key, kDirectories, value);
}

bool GetUserCrashDirectories(
    org::chromium::SessionManagerInterfaceProxyInterface* session_manager_proxy,
    std::vector<base::FilePath>* directories) {
  directories->clear();

  brillo::ErrorPtr error;
  std::map<std::string, std::string> sessions;
  session_manager_proxy->RetrieveActiveSessions(&sessions, &error);

  if (error) {
    LOG(ERROR) << "Error calling D-Bus proxy call to interface "
               << "'" << session_manager_proxy->GetObjectPath().value()
               << "': " << error->GetMessage();
    return false;
  }

  for (const auto& iter : sessions) {
    directories->push_back(
        paths::Get(brillo::cryptohome::home::GetHashedUserPath(iter.second)
                       .Append("crash")
                       .value()));
  }

  return true;
}

std::vector<unsigned char> GzipStream(brillo::StreamPtr data) {
  // Adapted from https://zlib.net/zlib_how.html
  z_stream deflate_stream;
  memset(&deflate_stream, 0, sizeof(deflate_stream));
  deflate_stream.zalloc = Z_NULL;
  deflate_stream.zfree = Z_NULL;

  // Using a window size of 31 sets us to gzip mode (16) + default window size
  // (15).
  const int kDefaultWindowSize = 15;
  const int kWindowSizeGzipAdd = 16;
  const int kDefaultMemLevel = 8;
  int result = deflateInit2(&deflate_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                            kDefaultWindowSize + kWindowSizeGzipAdd,
                            kDefaultMemLevel, Z_DEFAULT_STRATEGY);
  if (result != Z_OK) {
    LOG(ERROR) << "Error initializing zlib: error code " << result
               << ", error msg: "
               << (deflate_stream.msg == nullptr ? "None" : deflate_stream.msg);
    return std::vector<unsigned char>();
  }

  std::vector<unsigned char> deflated;
  int flush = Z_NO_FLUSH;
  /* compress until end of stream */
  do {
    size_t read_size = 0;
    unsigned char in[kBufferSize];
    if (!data->ReadBlocking(in, kBufferSize, &read_size, nullptr)) {
      // We are reading from a memory stream, so this really shouldn't happen.
      LOG(ERROR) << "Error reading from input stream";
      deflateEnd(&deflate_stream);
      return std::vector<unsigned char>();
    }
    if (data->GetRemainingSize() <= 0) {
      // We must request a flush on the last chunk of data, else deflateEnd
      // may just discard some compressed data.
      flush = Z_FINISH;
    }
    deflate_stream.next_in = in;
    deflate_stream.avail_in = read_size;
    do {
      unsigned char out[kBufferSize];
      deflate_stream.avail_out = kBufferSize;
      deflate_stream.next_out = out;
      result = deflate(&deflate_stream, flush);
      // Note that most return values are acceptable; don't error out if result
      // is not Z_OK. See discussion at https://zlib.net/zlib_how.html
      DCHECK_NE(result, Z_STREAM_ERROR);
      DCHECK_LE(deflate_stream.avail_out, kBufferSize);
      int amount_in_output_buffer = kBufferSize - deflate_stream.avail_out;
      deflated.insert(deflated.end(), out, out + amount_in_output_buffer);
    } while (deflate_stream.avail_out == 0);
    DCHECK_EQ(deflate_stream.avail_in, 0) << "deflate did not consume all data";
  } while (flush != Z_FINISH);
  DCHECK_EQ(result, Z_STREAM_END) << "Stream did not complete properly";
  deflateEnd(&deflate_stream);
  return deflated;
}

int RunAndCaptureOutput(brillo::ProcessImpl* process,
                        int fd,
                        std::string* output) {
  process->RedirectUsingPipe(fd, false);
  if (process->Start()) {
    const int out = process->GetPipe(fd);
    char buffer[kBufferSize];
    output->clear();

    while (true) {
      const ssize_t count = HANDLE_EINTR(read(out, buffer, kBufferSize));
      if (count < 0) {
        process->Wait();
        break;
      }

      if (count == 0)
        return process->Wait();

      output->append(buffer, count);
    }
  }

  return -1;
}

void LogMultilineError(const std::string& error) {
  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      error, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (auto line : lines)
    LOG(ERROR) << line;
}

bool ReadMemfdToString(int mem_fd, std::string* contents) {
  if (contents)
    contents->clear();
  base::ScopedFILE file(fdopen(mem_fd, "r"));
  if (!file) {
    PLOG(ERROR) << "Failed to fdopen(" << mem_fd << ")";
    return false;
  }
  if (fseeko(file.get(), 0, SEEK_END) != 0) {
    PLOG(ERROR) << "fseeko() error";
    return false;
  }
  off_t file_size = ftello(file.get());
  if (file_size < 0) {
    PLOG(ERROR) << "ftello() error";
    return false;
  } else if (file_size == 0) {
    LOG(ERROR) << "Minidump memfd has size of 0";
    return false;
  }

  if (fseeko(file.get(), 0, SEEK_SET) != 0) {
    PLOG(ERROR) << "fseeko() error";
    return false;
  }

  std::unique_ptr<char[]> buf(new char[file_size]);
  if (fread(buf.get(), 1, file_size, file.get()) != file_size) {
    PLOG(ERROR) << "fread() error";
    return false;
  }
  if (contents)
    contents->assign(buf.get(), file_size);

  return true;
}

int GetSelinuxWeight() {
  return 1000;
}

int GetServiceFailureWeight() {
  return 50;
}

}  // namespace util
