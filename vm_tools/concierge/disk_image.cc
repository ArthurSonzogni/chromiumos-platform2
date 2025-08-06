// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/disk_image.h"

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <unistd.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/uuid.h>
#include <vm_concierge/concierge_service.pb.h>

#include "vm_tools/concierge/plugin_vm_config.h"
#include "vm_tools/concierge/plugin_vm_helper.h"
#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/vmplugin_dispatcher_interface.h"

namespace {

constexpr gid_t kCrosvmUGid = 299;
constexpr uint32_t kZstdMagic = 0xFD2FB528;
constexpr uint32_t kZstdSeekSkippableFrameMagic = 0x184D2A5E;
constexpr uint32_t kZstdSeekFooterMagic = 0x8F92EAB1;

struct __attribute__((packed)) SeekTableFooter {
  uint32_t num_of_frames;
  uint8_t seek_table_descriptor;
  uint32_t magic;
};

static_assert(sizeof(SeekTableFooter) == 9);

int OutputFileOpenCallback(archive* a, void* data) {
  // We expect that we are writing into a regular file, so no padding is needed.
  archive_write_set_bytes_in_last_block(a, 1);
  return ARCHIVE_OK;
}

int OutputFileCloseCallback(archive* a, void* data) {
  return ARCHIVE_OK;
}

}  // namespace

namespace vm_tools::concierge {

DiskImageOperation::DiskImageOperation(const VmId vm_id)
    : uuid_(base::Uuid::GenerateRandomV4().AsLowercaseString()),
      vm_id_(std::move(vm_id)) {
  CHECK(base::Uuid::ParseCaseInsensitive(uuid_).is_valid());
}

void DiskImageOperation::Run(uint64_t io_limit) {
  if (ExecuteIo(io_limit)) {
    Finalize();
  }
}

int DiskImageOperation::GetProgress() const {
  if (status() == DISK_STATUS_IN_PROGRESS) {
    if (source_size_ == 0) {
      return 0;  // We do not know any better.
    }

    return processed_size_ * 100 / source_size_;
  }

  // Any other status indicates completed operation (successfully or not)
  // so return 100%.
  return 100;
}

std::unique_ptr<PluginVmCreateOperation> PluginVmCreateOperation::Create(
    base::ScopedFD fd,
    const base::FilePath& iso_dir,
    uint64_t source_size,
    const VmId vm_id,
    const std::vector<std::string> params) {
  auto op = base::WrapUnique(new PluginVmCreateOperation(
      std::move(fd), source_size, std::move(vm_id), std::move(params)));

  if (op->PrepareOutput(iso_dir)) {
    op->set_status(DISK_STATUS_IN_PROGRESS);
  }

  return op;
}

PluginVmCreateOperation::PluginVmCreateOperation(
    base::ScopedFD in_fd,
    uint64_t source_size,
    const VmId vm_id,
    const std::vector<std::string> params)
    : DiskImageOperation(std::move(vm_id)),
      params_(std::move(params)),
      in_fd_(std::move(in_fd)) {
  set_source_size(source_size);
}

bool PluginVmCreateOperation::PrepareOutput(const base::FilePath& iso_dir) {
  base::File::Error dir_error;

  if (!base::CreateDirectoryAndGetError(iso_dir, &dir_error)) {
    set_failure_reason(std::string("failed to create ISO directory: ") +
                       base::File::ErrorToString(dir_error));
    return false;
  }

  CHECK(output_dir_.Set(iso_dir));

  base::FilePath iso_path = iso_dir.Append("install.iso");
  out_fd_.reset(open(iso_path.value().c_str(), O_CREAT | O_WRONLY, 0660));
  if (!out_fd_.is_valid()) {
    PLOG(ERROR) << "Failed to create output ISO file " << iso_path.value();
    set_failure_reason("failed to create ISO file");
    return false;
  }

  return true;
}

void PluginVmCreateOperation::MarkFailed(const char* msg, int error_code) {
  if (error_code != 0) {
    set_status(error_code == ENOSPC ? DISK_STATUS_NOT_ENOUGH_SPACE
                                    : DISK_STATUS_FAILED);
    set_failure_reason(base::StringPrintf("%s: %s", msg, strerror(error_code)));
  } else {
    set_status(DISK_STATUS_FAILED);
    set_failure_reason(msg);
  }

  LOG(ERROR) << vm_id().name()
             << " PluginVm create operation failed: " << failure_reason();

  in_fd_.reset();
  out_fd_.reset();

  if (output_dir_.IsValid() && !output_dir_.Delete()) {
    LOG(WARNING) << "Failed to delete output directory on error";
  }
}

bool PluginVmCreateOperation::ExecuteIo(uint64_t io_limit) {
  do {
    uint8_t buf[65536];
    int count = HANDLE_EINTR(read(in_fd_.get(), buf, sizeof(buf)));
    if (count == 0) {
      // No more data
      return true;
    }

    if (count < 0) {
      MarkFailed("failed to read data block", errno);
      break;
    }

    int ret = HANDLE_EINTR(write(out_fd_.get(), buf, count));
    if (ret != count) {
      MarkFailed("failed to write data block", errno);
      break;
    }

    io_limit -= std::min(static_cast<uint64_t>(count), io_limit);
    AccumulateProcessedSize(count);
  } while (status() == DISK_STATUS_IN_PROGRESS && io_limit > 0);

  // More copying is to be done (or there was a failure).
  return false;
}

void PluginVmCreateOperation::Finalize() {
  // Close the file descriptors.
  in_fd_.reset();
  out_fd_.reset();

  if (!pvm::helper::CreateVm(vm_id(), std::move(params_))) {
    MarkFailed("Failed to create Plugin VM", 0);
    return;
  }

  if (!pvm::helper::AttachIso(vm_id(), "cdrom0",
                              pvm::plugin::kInstallIsoPath)) {
    MarkFailed("Failed to attach install ISO to Plugin VM", 0);
    pvm::helper::DeleteVm(vm_id());
    return;
  }

  if (!pvm::helper::CreateCdromDevice(vm_id(), pvm::plugin::kToolsIsoPath)) {
    MarkFailed("Failed to attach tools ISO to Plugin VM", 0);
    pvm::helper::DeleteVm(vm_id());
    return;
  }

  // Tell it not to try cleaning directory containing our ISO as we are
  // committed to using the image.
  output_dir_.Take();

  set_status(DISK_STATUS_CREATED);
}

std::unique_ptr<PluginVmExportOperation> PluginVmExportOperation::Create(
    const VmId vm_id,
    const base::FilePath disk_path,
    base::ScopedFD fd,
    base::ScopedFD digest_fd) {
  auto op = base::WrapUnique(
      new PluginVmExportOperation(std::move(vm_id), std::move(disk_path),
                                  std::move(fd), std::move(digest_fd)));

  if (op->PrepareInput() && op->PrepareOutput()) {
    op->set_status(DISK_STATUS_IN_PROGRESS);
  }

  return op;
}

PluginVmExportOperation::PluginVmExportOperation(const VmId vm_id,
                                                 const base::FilePath disk_path,
                                                 base::ScopedFD out_fd,
                                                 base::ScopedFD out_digest_fd)
    : DiskImageOperation(std::move(vm_id)),
      src_image_path_(std::move(disk_path)),
      out_fd_(std::move(out_fd)),
      out_digest_fd_(std::move(out_digest_fd)),
      sha256_(crypto::SecureHash::Create(crypto::SecureHash::SHA256)) {
  base::File::Info info;
  if (GetFileInfo(src_image_path_, &info) && !info.is_directory) {
    set_source_size(info.size);
    image_is_directory_ = false;
  } else {
    set_source_size(ComputeDirectorySize(src_image_path_));
    image_is_directory_ = true;
  }
}

PluginVmExportOperation::~PluginVmExportOperation() {
  // Ensure that the archive reader and writers are destroyed first, as these
  // can invoke callbacks that rely on data in this object.
  in_.reset();
  out_.reset();
}

bool PluginVmExportOperation::PrepareInput() {
  in_ = ArchiveReader(archive_read_disk_new());
  if (!in_) {
    set_failure_reason("libarchive: failed to create reader");
    return false;
  }

  // Do not cross mount points and do not archive chattr and xattr attributes.
  archive_read_disk_set_behavior(
      in_.get(), ARCHIVE_READDISK_NO_TRAVERSE_MOUNTS |
                     ARCHIVE_READDISK_NO_FFLAGS | ARCHIVE_READDISK_NO_XATTR);
  // Do not traverse symlinks.
  archive_read_disk_set_symlink_physical(in_.get());

  int ret = archive_read_disk_open(in_.get(), src_image_path_.value().c_str());
  if (ret != ARCHIVE_OK) {
    set_failure_reason("failed to open source directory as an archive");
    return false;
  }

  return true;
}

bool PluginVmExportOperation::PrepareOutput() {
  out_ = ArchiveWriter(archive_write_new());
  if (!out_) {
    set_failure_reason("libarchive: failed to create writer");
    return false;
  }

  int ret = archive_write_set_format_zip(out_.get());
  if (ret != ARCHIVE_OK) {
    set_failure_reason(base::StringPrintf(
        "libarchive: failed to initialize zip format: %s, %s",
        archive_error_string(out_.get()), strerror(archive_errno(out_.get()))));
    return false;
  }

  ret = archive_write_set_options(out_.get(), "compression-level=1");
  if (ret != ARCHIVE_OK) {
    set_failure_reason(base::StringPrintf(
        "libarchive: failed to set compression level: %s, %s",
        archive_error_string(out_.get()), strerror(archive_errno(out_.get()))));
    return false;
  }

  ret = archive_write_open(out_.get(), reinterpret_cast<void*>(this),
                           OutputFileOpenCallback, OutputFileWriteCallback,
                           OutputFileCloseCallback);
  if (ret != ARCHIVE_OK) {
    set_failure_reason("failed to open output archive");
    return false;
  }

  return true;
}

// static
ssize_t PluginVmExportOperation::OutputFileWriteCallback(archive* a,
                                                         void* data,
                                                         const void* buf,
                                                         size_t length) {
  PluginVmExportOperation* op =
      reinterpret_cast<PluginVmExportOperation*>(data);

  ssize_t bytes_written = HANDLE_EINTR(write(op->out_fd_.get(), buf, length));
  if (bytes_written <= 0) {
    archive_set_error(a, errno, "Write error");
    return -1;
  }

  op->sha256_->Update(buf, bytes_written);
  return bytes_written;
}

void PluginVmExportOperation::MarkFailed(const char* msg, struct archive* a) {
  if (a) {
    set_status(archive_errno(a) == ENOSPC ? DISK_STATUS_NOT_ENOUGH_SPACE
                                          : DISK_STATUS_FAILED);
    set_failure_reason(base::StringPrintf("%s: %s, %s", msg,
                                          archive_error_string(a),
                                          strerror(archive_errno(a))));
  } else {
    set_status(DISK_STATUS_FAILED);
    set_failure_reason(msg);
  }

  LOG(ERROR) << "Vm export failed: " << failure_reason();

  // Release resources.
  out_.reset();
  out_fd_.reset();
  out_digest_fd_.reset();
  in_.reset();
}

bool PluginVmExportOperation::ExecuteIo(uint64_t io_limit) {
  do {
    if (!copying_data_) {
      struct archive_entry* entry;
      int ret = archive_read_next_header(in_.get(), &entry);
      if (ret == ARCHIVE_EOF) {
        // Successfully copied entire archive.
        return true;
      }

      if (ret < ARCHIVE_OK) {
        MarkFailed("failed to read header", in_.get());
        break;
      }

      // Signal our intent to descend into directory (noop if current entry
      // is not a directory).
      archive_read_disk_descend(in_.get());

      const char* c_path = archive_entry_pathname(entry);
      if (!c_path || c_path[0] == '\0') {
        MarkFailed("archive entry read from disk has empty file name", nullptr);
        break;
      }

      base::FilePath path(c_path);
      if (image_is_directory_) {
        if (path == src_image_path_) {
          // Skip the image directory entry itself, as we will be storing
          // and restoring relative paths.
          continue;
        }

        // Strip the leading directory data as we want relative path,
        // and replace it with <vm_name>.pvm prefix.
        base::FilePath dest_path(vm_id().name() + ".pvm");
        if (!src_image_path_.AppendRelativePath(path, &dest_path)) {
          MarkFailed("failed to transform archive entry name", nullptr);
          break;
        }
        archive_entry_set_pathname(entry, dest_path.value().c_str());
      } else {
        archive_entry_set_pathname(entry, path.BaseName().value().c_str());
      }

      ret = archive_write_header(out_.get(), entry);
      if (ret != ARCHIVE_OK) {
        MarkFailed("failed to write header", out_.get());
        break;
      }

      copying_data_ = archive_entry_size(entry) > 0;
    }

    if (copying_data_) {
      uint64_t bytes_read = CopyEntry(io_limit);
      io_limit -= std::min(bytes_read, io_limit);
      AccumulateProcessedSize(bytes_read);
    }

    if (!copying_data_) {
      int ret = archive_write_finish_entry(out_.get());
      if (ret != ARCHIVE_OK) {
        MarkFailed("failed to finish entry", out_.get());
        break;
      }
    }
  } while (status() == DISK_STATUS_IN_PROGRESS && io_limit > 0);

  // More copying is to be done (or there was a failure).
  return false;
}

uint64_t PluginVmExportOperation::CopyEntry(uint64_t io_limit) {
  uint64_t bytes_read = 0;

  do {
    uint8_t buf[16384];
    int count = archive_read_data(in_.get(), buf, sizeof(buf));
    if (count == 0) {
      // No more data
      copying_data_ = false;
      break;
    }

    if (count < 0) {
      MarkFailed("failed to read data block", in_.get());
      break;
    }

    bytes_read += count;

    int ret = archive_write_data(out_.get(), buf, count);
    if (ret < ARCHIVE_OK) {
      MarkFailed("failed to write data block", out_.get());
      break;
    }
  } while (bytes_read < io_limit);

  return bytes_read;
}

void PluginVmExportOperation::Finalize() {
  archive_read_close(in_.get());
  // Free the input archive.
  in_.reset();

  int ret = archive_write_close(out_.get());
  if (ret != ARCHIVE_OK) {
    MarkFailed("libarchive: failed to close writer", out_.get());
    return;
  }
  // Free the output archive structures.
  out_.reset();
  // Close the file descriptor.
  out_fd_.reset();

  // Calculate and store the image hash.
  if (out_digest_fd_.is_valid()) {
    std::vector<uint8_t> digest(sha256_->GetHashLength());
    sha256_->Finish(std::data(digest), digest.size());
    std::string str = base::StringPrintf(
        "%s\n", base::HexEncode(std::data(digest), digest.size()).c_str());
    bool written = base::WriteFileDescriptor(out_digest_fd_.get(), str);
    out_digest_fd_.reset();
    if (!written) {
      LOG(ERROR) << "Failed to write SHA256 digest of the exported image";
      set_status(DISK_STATUS_FAILED);
      return;
    }
  }

  set_status(DISK_STATUS_CREATED);
}

std::unique_ptr<TerminaVmExportOperation> TerminaVmExportOperation::Create(
    const VmId vm_id,
    const base::FilePath disk_path,
    base::ScopedFD fd,
    base::ScopedFD digest_fd) {
  auto op = base::WrapUnique(
      new TerminaVmExportOperation(std::move(vm_id), std::move(disk_path),
                                   std::move(fd), std::move(digest_fd)));

  if (op->PrepareInput() && op->PrepareOutput()) {
    op->set_status(DISK_STATUS_IN_PROGRESS);
  }

  return op;
}

TerminaVmExportOperation::TerminaVmExportOperation(
    const VmId vm_id,
    const base::FilePath disk_path,
    base::ScopedFD out_fd,
    base::ScopedFD out_digest_fd)
    : DiskImageOperation(std::move(vm_id)),
      src_image_path_(std::move(disk_path)),
      out_fd_(std::move(out_fd)),
      out_digest_fd_(std::move(out_digest_fd)),
      sha256_(crypto::SecureHash::Create(crypto::SecureHash::SHA256)) {}

TerminaVmExportOperation::~TerminaVmExportOperation() {
  // Ensure that the archive reader and writers are destroyed first, as these
  // can invoke callbacks that rely on data in this object.
  in_.reset();
  out_.reset();
}

bool TerminaVmExportOperation::PrepareInput() {
  base::File::Info info;
  bool file_info_status = GetFileInfo(src_image_path_, &info);
  if (!file_info_status) {
    set_failure_reason("Failed to get file info");
    return false;
  }
  if (info.is_directory) {
    set_failure_reason("TerminaVmExport doesn't support directory input");
    return false;
  }

  set_source_size(info.size * 2);

  in_ = ArchiveReader(archive_read_disk_new());
  if (!in_) {
    set_failure_reason("libarchive: failed to create reader");
    return false;
  }

  // Do not cross mount points and do not archive chattr and xattr attributes.
  archive_read_disk_set_behavior(
      in_.get(), ARCHIVE_READDISK_NO_TRAVERSE_MOUNTS |
                     ARCHIVE_READDISK_NO_FFLAGS | ARCHIVE_READDISK_NO_XATTR);
  // Do not traverse symlinks.
  archive_read_disk_set_symlink_physical(in_.get());

  int ret = archive_read_disk_open(in_.get(), src_image_path_.value().c_str());
  if (ret != ARCHIVE_OK) {
    set_failure_reason("failed to open source directory as an archive");
    return false;
  }

  return true;
}

bool TerminaVmExportOperation::PrepareOutput() {
  out_ = ArchiveWriter(archive_write_new());
  if (!out_) {
    set_failure_reason("libarchive: failed to create writer");
    return false;
  }

  int ret;

  ret = archive_write_add_filter_zstd(out_.get());
  if (ret != ARCHIVE_OK) {
    set_failure_reason(base::StringPrintf(
        "libarchive: failed to initialize zstd ouptut filter: %s, %s",
        archive_error_string(out_.get()), strerror(archive_errno(out_.get()))));
    return false;
  }

  ret = archive_write_set_format_raw(out_.get());
  if (ret != ARCHIVE_OK) {
    set_failure_reason(base::StringPrintf(
        "libarchive: failed to initialize raw format: %s, %s",
        archive_error_string(out_.get()), strerror(archive_errno(out_.get()))));
    return false;
  }

  ret = archive_write_set_filter_option(out_.get(), "zstd", "compression-level",
                                        "4");
  if (ret != ARCHIVE_OK) {
    set_failure_reason(base::StringPrintf(
        "libarchive: failed to set compression level: %s, %s",
        archive_error_string(out_.get()), strerror(archive_errno(out_.get()))));
    return false;
  }

  // 128 KiB = 131072 bytes
  ret = archive_write_set_filter_option(out_.get(), "zstd", "max-frame-in",
                                        "131072");
  if (ret != ARCHIVE_OK) {
    set_failure_reason(base::StringPrintf(
        "libarchive: failed to set max frame size: %s, %s",
        archive_error_string(out_.get()), strerror(archive_errno(out_.get()))));
    return false;
  }

  ret = archive_write_open(out_.get(), reinterpret_cast<void*>(this),
                           OutputFileOpenCallback, OutputFileWriteCallback,
                           OutputFileCloseCallback);
  if (ret != ARCHIVE_OK) {
    set_failure_reason("failed to open output archive");
    return false;
  }

  return true;
}

// static
ssize_t TerminaVmExportOperation::OutputFileWriteCallback(archive* a,
                                                          void* data,
                                                          const void* buf,
                                                          size_t length) {
  TerminaVmExportOperation* op =
      reinterpret_cast<TerminaVmExportOperation*>(data);

  ssize_t bytes_written = HANDLE_EINTR(write(op->out_fd_.get(), buf, length));
  if (bytes_written <= 0) {
    archive_set_error(a, errno, "Write error");
    return -1;
  }

  op->sha256_->Update(buf, bytes_written);
  return bytes_written;
}

void TerminaVmExportOperation::MarkFailed(const char* msg, struct archive* a) {
  if (a) {
    set_status(archive_errno(a) == ENOSPC ? DISK_STATUS_NOT_ENOUGH_SPACE
                                          : DISK_STATUS_FAILED);
    set_failure_reason(base::StringPrintf("%s: %s, %s", msg,
                                          archive_error_string(a),
                                          strerror(archive_errno(a))));
  } else {
    set_status(DISK_STATUS_FAILED);
    set_failure_reason(msg);
  }

  LOG(ERROR) << "Vm export failed: " << failure_reason();

  // Release resources.
  out_.reset();
  out_fd_.reset();
  out_digest_fd_.reset();
  in_.reset();
}

bool TerminaVmExportOperation::ExecuteIo(uint64_t io_limit) {
  int ret;
  switch (state_) {
    case kBeforeOnlyEntry:
      struct archive_entry* entry;
      ret = archive_read_next_header(in_.get(), &entry);
      if (ret == ARCHIVE_EOF) {
        // No entry available
        MarkFailed("no entry available to read from", in_.get());
        break;
      }

      if (ret < ARCHIVE_OK) {
        MarkFailed("failed to read header", in_.get());
        break;
      }

      ret = archive_write_header(out_.get(), entry);
      if (ret != ARCHIVE_OK) {
        MarkFailed("failed to write header", out_.get());
        break;
      }

      if (archive_entry_size(entry) <= 0) {
        MarkFailed("entry size is not greater than 0", in_.get());
        break;
      }
      state_ = kCopying;
      [[fallthrough]];
    case kCopying:
      AccumulateProcessedSize(CopyEntry(io_limit));
      break;
    case kFinishedCopy:
      ret = archive_write_finish_entry(out_.get());
      if (ret != ARCHIVE_OK) {
        MarkFailed("failed to finish entry", out_.get());
        break;
      }

      archive_read_close(in_.get());
      // Free the input archive.
      in_.reset();
      ret = archive_write_close(out_.get());
      if (ret != ARCHIVE_OK) {
        MarkFailed("libarchive: failed to close writer", out_.get());
        break;
      }
      // Free the output archive structures.
      out_.reset();

      // TODO(b/345311779): Add custom metadata skippable frame, should at
      // least contain VM name
      {
        struct stat st;
        if (fstat(out_fd_.get(), &st) < 0) {
          MarkFailed("Failed to stat output file", nullptr);
          break;
        }
        zstd_total_frame_size_ = st.st_size;
      }

      state_ = kCalculatingSeekTable;
      break;
    case kCalculatingSeekTable:
      do {
        // We guarantee seek_table_build_offset_ points to start of a frame
        // Read up to 128KiB into compressed buffer
        ssize_t bytes_read = HANDLE_EINTR(
            pread(out_fd_.get(), compressed_fb_.data(), compressed_fb_.size(),
                  seek_table_build_offset_));
        if (bytes_read < 0) {
          MarkFailed("Failed to read from output file", nullptr);
          break;
        } else if (bytes_read < 8) {
          // each zstd frame is at least 8 bytes
          MarkFailed("Read less than 8 bytes from output file", nullptr);
          break;
        }
        io_limit -= std::min(static_cast<uint64_t>(bytes_read), io_limit);
        // this supports both normal and skippable frame
        size_t frame_compressed_size =
            ZSTD_findFrameCompressedSize(compressed_fb_.data(), bytes_read);
        if (ZSTD_isError(frame_compressed_size)) {
          LOG(ERROR) << "failed find compressed frame size at: "
                     << seek_table_build_offset_;
          MarkFailed("Failed to find frame compressed size", nullptr);
          break;
        }
        if (frame_compressed_size > bytes_read) {
          MarkFailed("Compressed frame size exceeds available data", nullptr);
          break;
        }
        // libarchive uses streaming compression mode, we can assume content
        // size info is absent from frame header. Head straight for
        // decompression
        size_t decompressed_size =
            ZSTD_decompress(decompressed_fb_.data(), decompressed_fb_.size(),
                            compressed_fb_.data(), frame_compressed_size);
        if (ZSTD_isError(decompressed_size)) {
          MarkFailed("Failed to decompress frame", nullptr);
          break;
        }
        seek_table_entries_.push_back(
            {static_cast<uint32_t>(frame_compressed_size),
             static_cast<uint32_t>(decompressed_size)});

        seek_table_build_offset_ += frame_compressed_size;
        AccumulateProcessedSize(decompressed_size);
      } while (io_limit > 0 &&
               seek_table_build_offset_ < zstd_total_frame_size_);

      if (seek_table_build_offset_ >= zstd_total_frame_size_) {
        state_ = kWriteSeekTable;
        // Seek to end of file
        if (lseek(out_fd_.get(), 0, SEEK_END) < 0) {
          MarkFailed("Failed to seek to start of output file", nullptr);
          break;
        }

        ssize_t bytes_written;
        bytes_written = HANDLE_EINTR(
            write(out_fd_.get(), &kZstdSeekSkippableFrameMagic, 4));
        if (bytes_written != 4) {
          MarkFailed("failed to write seek table skippable magic", in_.get());
          break;
        }
        sha256_->Update(&kZstdSeekSkippableFrameMagic, 4);

        uint32_t frame_size = 8 * seek_table_entries_.size() + 9;
        bytes_written = HANDLE_EINTR(write(out_fd_.get(), &frame_size, 4));
        if (bytes_written != 4) {
          MarkFailed("failed to write seek table frame size", in_.get());
          break;
        }
        sha256_->Update(&frame_size, 4);
      }
      break;
    case kWriteSeekTable:
      while (io_limit > 0 &&
             seektable_entry_written < seek_table_entries_.size()) {
        ssize_t bytes_written = HANDLE_EINTR(write(
            out_fd_.get(), &seek_table_entries_[seektable_entry_written], 8));
        if (bytes_written != 8) {
          MarkFailed("failed to write seek table entry", nullptr);
          break;
        }
        sha256_->Update(&seek_table_entries_[seektable_entry_written], 8);
        io_limit -= std::min(io_limit, static_cast<uint64_t>(bytes_written));
        seektable_entry_written++;
      }
      if (seektable_entry_written >= seek_table_entries_.size()) {
        // Finished writing all seek table entries
        SeekTableFooter footer{
            .num_of_frames = static_cast<uint32_t>(seek_table_entries_.size()),
            .seek_table_descriptor = 0,
            .magic = kZstdSeekFooterMagic};
        ssize_t bytes_written = HANDLE_EINTR(write(out_fd_.get(), &footer, 9));
        if (bytes_written != 9) {
          MarkFailed("failed to write seek table footer", nullptr);
          break;
        }
        sha256_->Update(&footer, 9);
        return true;
      }
      break;
    default:
      MarkFailed("invalid state", nullptr);
  }

  // More copying is to be done (or there was a failure).
  return false;
}

uint64_t TerminaVmExportOperation::CopyEntry(uint64_t io_limit) {
  uint64_t bytes_read = 0;

  do {
    uint8_t buf[16384];
    int count = archive_read_data(in_.get(), buf, sizeof(buf));
    if (count == 0) {
      // No more data
      state_ = kFinishedCopy;
      break;
    }

    if (count < 0) {
      MarkFailed("failed to read data block", in_.get());
      break;
    }

    bytes_read += count;

    int ret = archive_write_data(out_.get(), buf, count);
    if (ret < ARCHIVE_OK) {
      MarkFailed("failed to write data block", out_.get());
      break;
    }
  } while (bytes_read < io_limit);

  return bytes_read;
}

void TerminaVmExportOperation::Finalize() {
  // Close the file descriptor.
  out_fd_.reset();

  // Calculate and store the image hash.
  if (out_digest_fd_.is_valid()) {
    std::vector<uint8_t> digest(sha256_->GetHashLength());
    sha256_->Finish(std::data(digest), digest.size());
    std::string str = base::StringPrintf(
        "%s\n", base::HexEncode(std::data(digest), digest.size()).c_str());
    bool written = base::WriteFileDescriptor(out_digest_fd_.get(), str);
    out_digest_fd_.reset();
    if (!written) {
      LOG(ERROR) << "Failed to write SHA256 digest of the exported image";
      set_status(DISK_STATUS_FAILED);
      return;
    }
  }

  set_status(DISK_STATUS_CREATED);
}

std::unique_ptr<TerminaVmImportOperation> TerminaVmImportOperation::Create(
    base::ScopedFD fd,
    const base::FilePath disk_path,
    uint64_t source_size,
    const VmId vm_id) {
  auto op = base::WrapUnique(new TerminaVmImportOperation(
      std::move(fd), source_size, std::move(disk_path), std::move(vm_id)));

  if (op->PrepareInput() && op->PrepareOutput()) {
    op->set_status(DISK_STATUS_IN_PROGRESS);
  }

  return op;
}

TerminaVmImportOperation::TerminaVmImportOperation(
    base::ScopedFD in_fd,
    uint64_t source_size,
    const base::FilePath disk_path,
    const VmId vm_id)
    : DiskImageOperation(std::move(vm_id)),
      dest_image_path_(std::move(disk_path)),
      in_fd_(std::move(in_fd)) {
  set_source_size(source_size);
}

TerminaVmImportOperation::~TerminaVmImportOperation() {
  // Ensure that the archive reader and writers are destroyed first, as these
  // can invoke callbacks that rely on data in this object.
  in_.reset();
  out_.reset();
}

bool TerminaVmImportOperation::PrepareInput() {
  // Test if input file has a valid zstd header
  // only standard frame will pass the test, normally skippable frame is not
  // used as first frame.
  size_t file_size = lseek(in_fd_.get(), 0, SEEK_END);
  if (file_size < 4) {
    set_failure_reason("input file too small to be valid");
    return false;
  }
  lseek(in_fd_.get(), 0, SEEK_SET);

  // read 4 bytes as a number
  uint32_t header_magic;
  if (HANDLE_EINTR(read(in_fd_.get(), &header_magic, 4)) != 4) {
    set_failure_reason("failed to read header");
    return false;
  }
  lseek(in_fd_.get(), 0, SEEK_SET);

  // compare header to ZSTD magic
  if (header_magic == kZstdMagic) {
    zstd_ = true;
  } else {
    zstd_ = false;
  }
  in_ = ArchiveReader(archive_read_new());
  if (!in_.get()) {
    set_failure_reason("libarchive: failed to create reader");
    return false;
  }

  int ret;

  if (zstd_) {
    ret = archive_read_support_format_raw(in_.get());
    if (ret != ARCHIVE_OK) {
      set_failure_reason("libarchive: failed to initialize raw format");
      return false;
    }

    ret = archive_read_support_filter_zstd(in_.get());
    if (ret != ARCHIVE_OK) {
      set_failure_reason("libarchive: failed to initialize zstd filter");
      return false;
    }
  } else {
    ret = archive_read_support_format_zip(in_.get());
    if (ret != ARCHIVE_OK) {
      set_failure_reason("libarchive: failed to initialize zip format");
      return false;
    }

    ret = archive_read_support_filter_all(in_.get());
    if (ret != ARCHIVE_OK) {
      set_failure_reason("libarchive: failed to initialize filter");
      return false;
    }
  }

  ret = archive_read_open_fd(in_.get(), in_fd_.get(), 102400);
  if (ret != ARCHIVE_OK) {
    set_failure_reason("failed to open input archive");
    return false;
  }

  return true;
}

bool TerminaVmImportOperation::PrepareOutput() {
  // We are not using CreateUniqueTempDirUnderPath() because we want
  // to be able to identify images that are being imported, and that
  // requires directory name to not be random.
  base::FilePath disk_path(dest_image_path_.AddExtension(".tmp"));
  if (base::PathExists(disk_path)) {
    set_failure_reason("VM with this name is already being imported");
    return false;
  }

  // Create a temp directory with a fixed name based on the disk image name to
  // ensure multiple import operations can't happen simultaneously for the
  // same VM.
  base::File::Error dir_error;
  if (!base::CreateDirectoryAndGetError(disk_path, &dir_error)) {
    set_failure_reason(std::string("failed to create output directory: ") +
                       base::File::ErrorToString(dir_error));
    return false;
  }

  CHECK(output_dir_.Set(disk_path));

  out_ = ArchiveWriter(archive_write_disk_new());
  if (!out_) {
    set_failure_reason("libarchive: failed to create writer");
    return false;
  }

  int ret = archive_write_disk_set_options(
      out_.get(), ARCHIVE_EXTRACT_SECURE_SYMLINKS |
                      ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_XATTR);
  if (ret != ARCHIVE_OK) {
    set_failure_reason("libarchive: failed to set disk options");
    return false;
  }

  return true;
}

void TerminaVmImportOperation::MarkFailed(const char* msg, struct archive* a) {
  if (a) {
    set_status(archive_errno(a) == ENOSPC ? DISK_STATUS_NOT_ENOUGH_SPACE
                                          : DISK_STATUS_FAILED);
    set_failure_reason(base::StringPrintf("%s: %s, %s", msg,
                                          archive_error_string(a),
                                          strerror(archive_errno(a))));
  } else {
    set_status(DISK_STATUS_FAILED);
    set_failure_reason(msg);
  }

  LOG(ERROR) << "TerminaVm import failed: " << failure_reason();

  // Release resources.
  out_.reset();
  if (output_dir_.IsValid() && !output_dir_.Delete()) {
    LOG(WARNING) << "Failed to delete output directory on error";
  }

  in_.reset();
  in_fd_.reset();
}

bool TerminaVmImportOperation::ExecuteIo(uint64_t io_limit) {
  do {
    if (!copying_data_) {
      struct archive_entry* entry;
      int ret = archive_read_next_header(in_.get(), &entry);
      if (ret == ARCHIVE_EOF) {
        // Successfully copied entire archive.
        return true;
      }

      if (ret < ARCHIVE_OK) {
        MarkFailed("failed to read header", in_.get());
        break;
      }

      const char* c_path = archive_entry_pathname(entry);
      if (!c_path || c_path[0] == '\0') {
        MarkFailed("archive entry has empty file name", nullptr);
        break;
      }

      base::FilePath path(c_path);

      mode_t mode = archive_entry_filetype(entry);

      // For zip archive:
      // The archive should contain a single file named the same as the
      // destination file ("dGVybWluYQ==.img" for termina).

      // For zstd compressed file:
      // The file is treated as a single entry archive with a generic entry
      // name
      base::FilePath dest_filename = dest_image_path_.BaseName();
      if ((!zstd_ && path != dest_filename) || mode != AE_IFREG) {
        LOG(ERROR) << "Expected TerminaVm image named " << dest_filename
                   << ", got " << path << " mode " << mode;
        MarkFailed("archive entry does not match expected file", nullptr);
        break;
      }

      base::FilePath dest_path = output_dir_.GetPath().Append(dest_filename);
      archive_entry_set_pathname(entry, dest_path.value().c_str());

      archive_entry_set_uid(entry, kCrosvmUGid);
      archive_entry_set_gid(entry, kCrosvmUGid);

      // Apply the xattr that would be set when installing a VM (not preserved
      // in export).
      static constexpr char xattr_val[] = "1";
      archive_entry_xattr_add_entry(
          entry, kDiskImagePreallocatedWithUserChosenSizeXattr, xattr_val,
          sizeof(xattr_val));

      archive_entry_set_perm(entry, 0660);

      ret = archive_write_header(out_.get(), entry);
      if (ret != ARCHIVE_OK) {
        MarkFailed("failed to write header", out_.get());
        break;
      }

      // zstd filter in libarchive does not have `read_header`
      // and its entry size is thus unset
      if (zstd_ && !archive_entry_size_is_set(entry)) {
        copying_data_ = true;
      } else {
        copying_data_ = archive_entry_size(entry) > 0;
      }
    }

    if (copying_data_) {
      uint64_t bytes_read = CopyEntry(io_limit);
      io_limit -= std::min(bytes_read, io_limit);
      AccumulateProcessedSize(bytes_read);
    }

    if (!copying_data_) {
      int ret = archive_write_finish_entry(out_.get());
      if (ret != ARCHIVE_OK) {
        MarkFailed("failed to finish entry", out_.get());
        break;
      }
    }
  } while (status() == DISK_STATUS_IN_PROGRESS && io_limit > 0);

  // More copying is to be done (or there was a failure).
  return false;
}

// Note that this is extremely similar to VmExportOperation::CopyEntry()
// implementation. The difference is the disk writer supports
// archive_write_data_block() API that handles sparse files, whereas generic
// writer does not, so we have to use separate implementations.
uint64_t TerminaVmImportOperation::CopyEntry(uint64_t io_limit) {
  uint64_t bytes_read_begin = archive_filter_bytes(in_.get(), -1);
  uint64_t bytes_read = 0;

  do {
    const void* buff;
    size_t size;
    la_int64_t offset;
    int ret = archive_read_data_block(in_.get(), &buff, &size, &offset);
    if (ret == ARCHIVE_EOF) {
      copying_data_ = false;
      break;
    }

    if (ret != ARCHIVE_OK) {
      MarkFailed("failed to read data block", in_.get());
      break;
    }

    bytes_read = archive_filter_bytes(in_.get(), -1) - bytes_read_begin;

    ret = archive_write_data_block(out_.get(), buff, size, offset);
    if (ret != ARCHIVE_OK) {
      MarkFailed("failed to write data block", out_.get());
      break;
    }
  } while (bytes_read < io_limit);
  return bytes_read;
}

void TerminaVmImportOperation::Finalize() {
  archive_read_close(in_.get());
  // Free the input archive.
  in_.reset();
  // Close the file descriptor.
  in_fd_.reset();

  int ret = archive_write_close(out_.get());
  if (ret != ARCHIVE_OK) {
    MarkFailed("libarchive: failed to close writer", out_.get());
    return;
  }
  // Free the output archive structures.
  out_.reset();

  // Move the disk image file to the top level where it belongs and remove the
  // temp directory.
  auto temp_disk_image_path =
      output_dir_.GetPath().Append(dest_image_path_.BaseName());
  base::File::Error err;
  if (!base::ReplaceFile(temp_disk_image_path, dest_image_path_, &err)) {
    LOG(ERROR) << "Unable to rename imported disk image: " << err;
    MarkFailed("Unable to rename imported disk image", nullptr);
    return;
  }

  if (!output_dir_.Delete()) {
    LOG(ERROR) << "Failed to delete temporary import directory";
  }

  set_status(DISK_STATUS_CREATED);
}

std::unique_ptr<VmResizeOperation> VmResizeOperation::Create(
    const VmId vm_id,
    StorageLocation location,
    const base::FilePath disk_path,
    uint64_t disk_size,
    StartResizeCallback start_resize_cb,
    ProcessResizeCallback process_resize_cb) {
  DiskImageStatus status = DiskImageStatus::DISK_STATUS_UNKNOWN;
  std::string failure_reason;
  std::move(start_resize_cb)
      .Run(vm_id, location, disk_size, &status, &failure_reason);

  auto op = base::WrapUnique(new VmResizeOperation(
      std::move(vm_id), std::move(location), std::move(disk_path),
      std::move(disk_size), std::move(process_resize_cb)));

  op->set_status(status);
  op->set_failure_reason(failure_reason);

  return op;
}

VmResizeOperation::VmResizeOperation(const VmId vm_id,
                                     StorageLocation location,
                                     const base::FilePath disk_path,
                                     uint64_t disk_size,
                                     ProcessResizeCallback process_resize_cb)
    : DiskImageOperation(std::move(vm_id)),
      process_resize_cb_(std::move(process_resize_cb)),
      location_(std::move(location)),
      disk_path_(std::move(disk_path)),
      target_size_(std::move(disk_size)) {}

bool VmResizeOperation::ExecuteIo(uint64_t io_limit) {
  DiskImageStatus status = DiskImageStatus::DISK_STATUS_UNKNOWN;
  std::string failure_reason;
  process_resize_cb_.Run(vm_id(), location_, target_size_, &status,
                         &failure_reason);

  set_status(status);
  set_failure_reason(failure_reason);

  if (status != DISK_STATUS_IN_PROGRESS) {
    return true;
  }

  return false;
}

void VmResizeOperation::Finalize() {}

}  // namespace vm_tools::concierge
