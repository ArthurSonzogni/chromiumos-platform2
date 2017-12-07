/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define FUSE_USE_VERSION 26

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse/fuse.h>
#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <string>

#define USER_NS_SHIFT 655360
#define MEDIA_RW_UID (1023 + USER_NS_SHIFT)
#define MEDIA_RW_GID (1023 + USER_NS_SHIFT)
#define CHRONOS_UID 1000
#define CHRONOS_GID 1000

#define WRAP_FS_CALL(res) ((res) < 0 ? -errno : 0)

static int passthrough_create(const char* path,
                              mode_t mode,
                              struct fuse_file_info* fi) {
  // Ignore specified |mode| and always use a fixed mode since we do not allow
  // chmod anyway.
  int fd = open(path, fi->flags, 0644);
  if (fd < 0) {
    return -errno;
  }
  fi->fh = fd;
  return 0;
}

static int passthrough_fgetattr(const char* path,
                                struct stat* buf,
                                struct fuse_file_info* fi) {
  int fd = static_cast<int>(fi->fh);
  // File owner is overridden by uid/gid options passed to fuse.
  return WRAP_FS_CALL(fstat(fd, buf));
}

static int passthrough_fsync(const char* path,
                             int datasync,
                             struct fuse_file_info* fi) {
  int fd = static_cast<int>(fi->fh);
  return datasync ? WRAP_FS_CALL(fdatasync(fd)) : WRAP_FS_CALL(fsync(fd));
}

static int passthrough_fsyncdir(const char* path,
                                int datasync,
                                struct fuse_file_info* fi) {
  DIR* dirp = reinterpret_cast<DIR*>(fi->fh);
  int fd = dirfd(dirp);
  return datasync ? WRAP_FS_CALL(fdatasync(fd)) : WRAP_FS_CALL(fsync(fd));
}

static int passthrough_ftruncate(const char* path,
                                 off_t size,
                                 struct fuse_file_info* fi) {
  int fd = static_cast<int>(fi->fh);
  return WRAP_FS_CALL(ftruncate(fd, size));
}

static int passthrough_getattr(const char* path, struct stat* buf) {
  // File owner is overridden by uid/gid options passed to fuse.
  return WRAP_FS_CALL(lstat(path, buf));
}

static int passthrough_mkdir(const char* path, mode_t mode) {
  return WRAP_FS_CALL(mkdir(path, mode));
}

static int passthrough_open(const char* path, struct fuse_file_info* fi) {
  int fd = open(path, fi->flags);
  if (fd < 0) {
    return -errno;
  }
  fi->fh = fd;
  return 0;
}

static int passthrough_opendir(const char* path, struct fuse_file_info* fi) {
  DIR* dirp = opendir(path);
  if (!dirp) {
    return -errno;
  }
  fi->fh = reinterpret_cast<uint64_t>(dirp);
  return 0;
}

static int passthrough_read(const char* path,
                            char* buf,
                            size_t size,
                            off_t off,
                            struct fuse_file_info* fi) {
  int fd = static_cast<int>(fi->fh);
  int res = pread(fd, buf, size, off);
  if (res < 0) {
    return -errno;
  }
  return res;
}

/*
// TODO(nya): Use this faster version after we update libfuse.
static int passthrough_read_buf(
    const char* path, struct fuse_bufvec** srcp, size_t size, off_t off,
    struct fuse_file_info* fi) {
  int fd = static_cast<int>(fi->fh);
  struct fuse_bufvec* src = static_cast<struct fuse_bufvec*>(
      malloc(sizeof(struct fuse_bufvec)));
  *src = FUSE_BUFVEC_INIT(0);
  src->buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
  src->buf[0].fd = fd;
  src->buf[0].pos = off;
  *srcp = src;
  return 0;
}
*/

static int passthrough_readdir(const char* path,
                               void* buf,
                               fuse_fill_dir_t filler,
                               off_t off,
                               struct fuse_file_info* fi) {
  // TODO(nya): This implementation returns all files at once and thus
  // inefficient. Make use of offset and be better to memory.
  DIR* dirp = reinterpret_cast<DIR*>(fi->fh);
  errno = 0;
  for (;;) {
    struct dirent* entry = readdir(dirp);
    if (entry == nullptr) {
      break;
    }
    // Only IF part of st_mode matters. See fill_dir() in fuse.c.
    struct stat stbuf = {};
    stbuf.st_mode = DTTOIF(entry->d_type);
    filler(buf, entry->d_name, &stbuf, 0);
  }
  return -errno;
}

static int passthrough_release(const char* path, struct fuse_file_info* fi) {
  int fd = static_cast<int>(fi->fh);
  return WRAP_FS_CALL(close(fd));
}

static int passthrough_releasedir(const char* path, struct fuse_file_info* fi) {
  DIR* dirp = reinterpret_cast<DIR*>(fi->fh);
  return WRAP_FS_CALL(closedir(dirp));
}

static int passthrough_rename(const char* oldpath, const char* newpath) {
  return WRAP_FS_CALL(rename(oldpath, newpath));
}

static int passthrough_rmdir(const char* path) {
  return WRAP_FS_CALL(rmdir(path));
}

static int passthrough_statfs(const char* path, struct statvfs* buf) {
  return WRAP_FS_CALL(statvfs(path, buf));
}

static int passthrough_truncate(const char* path, off_t size) {
  return WRAP_FS_CALL(truncate(path, size));
}

static int passthrough_unlink(const char* path) {
  return WRAP_FS_CALL(unlink(path));
}

static int passthrough_utimens(const char* path, const struct timespec tv[2]) {
  return WRAP_FS_CALL(utimensat(AT_FDCWD, path, tv, 0));
}

static int passthrough_write(const char* path,
                             const char* buf,
                             size_t size,
                             off_t off,
                             struct fuse_file_info* fi) {
  int fd = static_cast<int>(fi->fh);
  int res = pwrite(fd, buf, size, off);
  if (res < 0) {
    return -errno;
  }
  return res;
}

/*
// TODO(nya): Use this faster version after we update libfuse.
static int passthrough_write_buf(
    const char* path, const fuse_bufvec* src, off_t off,
    struct fuse_file_info* fi) {
  int fd = static_cast<int>(fi->fh);
  struct fuse_bufvec dst = FUSE_BUFVEC_INIT(0);
  dst.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
  dst.buf[0].fd = fd;
  dst.buf[0].pos = off;
  return fuse_buf_copy(&dst, src, 0);
}
*/

static void setup_passthrough_ops(struct fuse_operations* passthrough_ops) {
  memset(passthrough_ops, 0, sizeof(*passthrough_ops));
#define FILL_OP(name) passthrough_ops->name = passthrough_##name
  FILL_OP(create);
  FILL_OP(fgetattr);
  FILL_OP(fsync);
  FILL_OP(fsyncdir);
  FILL_OP(ftruncate);
  FILL_OP(getattr);
  FILL_OP(mkdir);
  FILL_OP(open);
  FILL_OP(opendir);
  FILL_OP(read);
  FILL_OP(readdir);
  FILL_OP(release);
  FILL_OP(releasedir);
  FILL_OP(rename);
  FILL_OP(rmdir);
  FILL_OP(statfs);
  FILL_OP(truncate);
  FILL_OP(unlink);
  FILL_OP(utimens);
  FILL_OP(write);
#undef FILL_OP
}

int main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <source> <destination> <umask>\n", argv[0]);
    return 1;
  }

  if (getuid() != CHRONOS_UID) {
    fprintf(stderr, "This daemon must run as chronos user.\n");
    return 1;
  }
  if (getgid() != CHRONOS_GID) {
    fprintf(stderr, "This daemon must run as chronos group.\n");
    return 1;
  }

  struct fuse_operations passthrough_ops;
  setup_passthrough_ops(&passthrough_ops);

  std::string fuse_subdir_opt(std::string("subdir=") + argv[1]);
  std::string fuse_uid_opt(std::string("uid=") + std::to_string(MEDIA_RW_UID));
  std::string fuse_gid_opt(std::string("gid=") + std::to_string(MEDIA_RW_GID));
  std::string fuse_umask_opt(std::string("umask=") + argv[3]);

  const char* fuse_argv[] = {
      argv[0], argv[2], "-f", "-o", "allow_other", "-o", "default_permissions",
      // Never cache attr/dentry since our backend storage is not exclusive to
      // this process.
      "-o", "attr_timeout=0", "-o", "entry_timeout=0", "-o",
      "negative_timeout=0", "-o", "ac_attr_timeout=0", "-o",
      "fsname=passthrough", "-o", fuse_uid_opt.c_str(), "-o",
      fuse_gid_opt.c_str(), "-o", "modules=subdir", "-o",
      fuse_subdir_opt.c_str(), "-o", "direct_io", "-o", fuse_umask_opt.c_str(),
  };
  int fuse_argc = sizeof(fuse_argv) / sizeof(fuse_argv[0]);

  umask(0022);
  return fuse_main(fuse_argc, const_cast<char**>(fuse_argv), &passthrough_ops,
                   NULL);
}
