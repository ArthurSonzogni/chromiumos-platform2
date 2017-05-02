// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_PLATFORM_H_
#define CRYPTOHOME_PLATFORM_H_

#include <stdint.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest_prod.h>

extern "C" {
#include <keyutils.h>
}

#include "cryptohome/dircrypto_util.h"

namespace base {
class Thread;
class Time;
}  // namespace base

namespace cryptohome {

// Default umask
extern const int kDefaultUmask;

class ProcessInformation;

// A class for enumerating the files in a provided path. The order of the
// results is not guaranteed.
//
// DO NOT USE FROM THE MAIN THREAD of your application unless it is a test
// program where latency does not matter. This class is blocking.
//
// See base::FileEnumerator for details.  This is merely a mockable wrapper.
class FileEnumerator {
 public:
  // Copy and assign enabled.
  class FileInfo {
   public:
    explicit FileInfo(const base::FileEnumerator::FileInfo& file_info);
    FileInfo(const base::FilePath& name, const struct stat& stat);
    FileInfo(const FileInfo& other);
    FileInfo();
    virtual ~FileInfo();
    FileInfo& operator=(const FileInfo& other);

    bool IsDirectory() const;
    base::FilePath GetName() const;
    int64_t GetSize() const;
    base::Time GetLastModifiedTime() const;
    const struct stat& stat() const;

   private:
    void Assign(const base::FileEnumerator::FileInfo& file_info);

    std::unique_ptr<base::FileEnumerator::FileInfo> info_;
    base::FilePath name_;
    struct stat stat_;
  };

  FileEnumerator(const base::FilePath& root_path,
                 bool recursive,
                 int file_type);
  FileEnumerator(const base::FilePath& root_path,
                 bool recursive,
                 int file_type,
                 const std::string& pattern);
  // Meant for testing only.
  FileEnumerator();
  virtual ~FileEnumerator();

  // Returns an empty file name if there are no more results.
  virtual base::FilePath Next();

  // Write the file info into |info|.
  virtual FileInfo GetInfo();

 private:
  std::unique_ptr<base::FileEnumerator> enumerator_;
};

// Platform specific routines abstraction layer.
// Also helps us to be able to mock them in tests.
class Platform {
 public:
  struct Permissions {
    uid_t user;
    gid_t group;
    mode_t mode;
  };

  typedef base::Callback<bool(const base::FilePath&, const struct stat&)>
      FileEnumeratorCallback;

  Platform();

  virtual ~Platform();

  // Calls the platform mount
  //
  // Parameters
  //   from - The node to mount from
  //   to - The node to mount to
  //   type - The fs type
  //   mount_options - The mount options to pass to mount()
  virtual bool Mount(const base::FilePath& from, const base::FilePath& to,
                     const std::string& type, const std::string& mount_options);

  // Creates a bind mount
  //
  // Parameters
  //   from - Where to mount from
  //   to - Where to mount to
  virtual bool Bind(const base::FilePath& from, const base::FilePath& to);

  // Calls the platform unmount
  //
  // Parameters
  //   path - The path to unmount
  //   lazy - Whether to call a lazy unmount
  //   was_busy (OUT) - Set to true on return if the mount point was busy
  virtual bool Unmount(const base::FilePath& path, bool lazy, bool* was_busy);

  // Lazily unmounts |path|.
  //
  // Parameters
  //   path - The destination path to unmount
  virtual void LazyUnmount(const base::FilePath& path);

  // Returns true if any mounts match. Populates |mounts| if
  // any mount sources have a matching prefix (|from_prefix|).
  //
  // Parameters
  //   from_prefix - Prefix for matching mount sources
  //   mounts - matching mounted paths, may be NULL
  virtual bool GetMountsBySourcePrefix(const base::FilePath& from_prefix,
      std::multimap<const base::FilePath, const base::FilePath>* mounts);

  // Returns true if the directory is in the mount_info
  //
  // Parameters
  //   directory - The directory to check
  virtual bool IsDirectoryMounted(const base::FilePath& directory);


  // GetProcessesWithOpenFiles
  //
  // Parameters
  //   path - The path to check if the process has open files on
  //   pids (OUT) - The PIDs found
  virtual void GetProcessesWithOpenFiles(const base::FilePath& path_in,
                                 std::vector<ProcessInformation>* processes);

  // Calls the platform stat() or lstat() function to obtain the ownership of
  // a given path. The path may be a directory or a file.
  //
  // Parameters
  //   path - The path to look up
  //   user_id - The user ID of the path. NULL if the result is not needed.
  //   group_id - The group ID of the path. NULL if the result is not needed.
  //   follow_links - Whether or not to dereference symlinks
  virtual bool GetOwnership(const base::FilePath& path,
                            uid_t* user_id,
                            gid_t* group_id,
                            bool follow_links) const;

  // Calls the platform chown() or lchown() function on the given path.
  //
  // The path may be a directory or a file.
  //
  // Parameters
  //   path - The path to set ownership on
  //   user_id - The user_id to assign ownership to
  //   group_id - The group_id to assign ownership to
  //   follow_links - Whether or not to dereference symlinks
  virtual bool SetOwnership(const base::FilePath& directory,
                            uid_t user_id,
                            gid_t group_id,
                            bool follow_links) const;

  // Calls the platform stat() function to obtain the permissions of
  // the given path. The path may be a directory or a file.
  //
  // Parameters
  //   path - The path to look up
  //   mode - The permissions of the path
  virtual bool GetPermissions(const base::FilePath& path, mode_t* mode) const;

  // Calls the platform chmod() function on the given path.
  // The path may be a directory or a file.
  //
  // Parameters
  //   path - The path to change the permissions on
  //   mode - the mode to change the permissions to
  virtual bool SetPermissions(const base::FilePath& path, mode_t mode) const;

  // Sets the path accessible by a group with specified permissions
  //
  // Parameters
  //   path - The path to change the ownership and permissions on
  //   group_id - The group ID to assign to the path
  //   group_mode - The group permissions to assign to the path
  virtual bool SetGroupAccessible(const base::FilePath& path,
                                  gid_t group_id,
                                  mode_t group_mode) const;

  // Applies ownership and permissions recursively if they do not already match.
  // Logs a warning each time ownership or permissions need to be set.
  //
  // Parameters
  //   path - The base path.
  //   default_file_info - Default ownership / perms for files.
  //   default_dir_info - Default ownership / perms for directories.
  //   special_cases - A map of absolute path to ownership / perms.
  virtual bool ApplyPermissionsRecursive(
      const base::FilePath& path,
      const Permissions& default_file_info,
      const Permissions& default_dir_info,
      const std::map<base::FilePath, Permissions>& special_cases);

  // Sets the current umask, returning the old mask
  //
  // Parameters
  //   new_mask - The mask to set
  virtual int SetMask(int new_mask) const;

  // Returns the user and group ids for a user
  //
  // Parameters
  //   user - The username to query for
  //   user_id (OUT) - The user ID on success
  //   group_id (OUT) - The group ID on success
  virtual bool GetUserId(const std::string& user, uid_t* user_id,
                         gid_t* group_id) const;

  // Returns the group id for a group
  //
  // Parameters
  //   group - The group name to query for
  //   group_id (OUT) - The group ID on success
  virtual bool GetGroupId(const std::string& group, gid_t* group_id) const;

  // Return the available disk space in bytes on the volume containing |path|,
  // or -1 on failure.
  // Code duplicated from Chrome's base::SysInfo::AmountOfFreeDiskSpace().
  //
  // Parameters
  //   path - the pathname of any file within the mounted file system
  virtual int64_t AmountOfFreeDiskSpace(const base::FilePath& path) const;

  // Returns true if the specified file exists.
  //
  // Parameters
  //  path - Path of the file to check
  virtual bool FileExists(const base::FilePath& path);

  // Check if a directory exists as the given path
  virtual bool DirectoryExists(const base::FilePath& path);

  // Provides the size of a file at |path| if it exists.
  //
  // Parameters
  //   path - Path of the file to check
  //   size - int64_t* to populate with the size
  // Returns true if the size was acquired and false otherwise.
  virtual bool GetFileSize(const base::FilePath& path, int64_t* size);

  // Returns the size of a directory at |path| if it exists.
  //
  // Parameters
  //   path - Path of the directory to check
  // Returns the directory size if it was acquired, and -1 on failure.
  virtual int64_t ComputeDirectorySize(const base::FilePath& path);

  // Opens a file, if possible, returning a FILE*. If not, returns NULL.
  //
  // Parameters
  //   path - Path of the file to open
  //   mode - mode string of the file when opened
  virtual FILE* OpenFile(const base::FilePath& path, const char* mode);

  // Closes a FILE* opened with OpenFile()
  //
  // Parameters
  //  fp - FILE* to close
  virtual bool CloseFile(FILE* fp);

  // Creates and opens a temporary file if possible.
  //
  // Parameters
  //  path - Pointer to where the file is created if successful.
  virtual FILE* CreateAndOpenTemporaryFile(base::FilePath* path);

  // Initializes a base::File.  The caller is responsible for verifying that
  // the file was successfully opened by calling base::File::IsValid().
  //
  // Parameters
  //   file - The base::File object to open the file in.
  //   path - Path of the file to open.
  //   flags - Flags to use when opening the file.  See base::File::Flags.
  virtual void InitializeFile(base::File* file,
                              const base::FilePath& path,
                              uint32_t flags);

  // Reads a file completely into a blob/string.
  //
  // Parameters
  //  path              - Path of the file to read
  //  blob/string (OUT) - blob/string to populate
  virtual bool ReadFile(const base::FilePath& path, brillo::Blob* blob);
  virtual bool ReadFileToString(const base::FilePath& path,
                                std::string* string);

  // Writes to the open file pointer.
  //
  // Parameters
  //   fp   - pointer to the FILE*
  //   blob - data to write
  virtual bool WriteOpenFile(FILE* fp, const brillo::Blob& blob);

  // Writes the entirety of the given data to |path| with 0640 permissions
  // (modulo umask).  If missing, parent (and parent of parent etc.) directories
  // are created with 0700 permissions (modulo umask).  Returns true on success.
  //
  // Parameters
  //  path      - Path of the file to write
  //  blob/data - blob/string/array to populate from
  // (size      - array size)
  virtual bool WriteFile(const base::FilePath& path, const brillo::Blob& blob);
  virtual bool WriteStringToFile(const base::FilePath& path,
                                 const std::string& data);
  virtual bool WriteArrayToFile(const base::FilePath& path, const char* data,
                                size_t size);

  // Atomically writes the entirety of the given data to |path| with |mode|
  // permissions (modulo umask).  If missing, parent (and parent of parent etc.)
  // directories are created with 0700 permissions (modulo umask).  Returns true
  // if the file has been written successfully and it has physically hit the
  // disk.  Returns false if either writing the file has failed or if it cannot
  // be guaranteed that it has hit the disk.
  //
  // Parameters
  //   path - Path of the file to write
  //   data - Blob to populate from
  //   mode - File permission bit-pattern, eg. 0644 for rw-r--r--
  virtual bool WriteFileAtomic(const base::FilePath& path,
                               const brillo::Blob& blob,
                               mode_t mode);
  virtual bool WriteStringToFileAtomic(const base::FilePath& path,
                                       const std::string& data,
                                       mode_t mode);

  // Atomically and durably writes the entirety of the given data to |path| with
  // |mode| permissions (modulo umask).  If missing, parent (and parent of
  // parent etc.)  directories are created with 0700 permissions (modulo umask).
  // Returns true if the file has been written successfully and it has
  // physically hit the disk.  Returns false if either writing the file has
  // failed or if it cannot be guaranteed that it has hit the disk.
  //
  // Parameters
  //  path      - Path of the file to write
  //  blob/data - blob/string to populate from
  //  mode      - File permission bit-pattern, eg. 0644 for rw-r--r--
  virtual bool WriteFileAtomicDurable(const base::FilePath& path,
                                      const brillo::Blob& blob,
                                      mode_t mode);
  virtual bool WriteStringToFileAtomicDurable(const base::FilePath& path,
                                              const std::string& data,
                                              mode_t mode);

  // Creates empty file durably, i.e. ensuring that the directory entry is
  // created on-disk immediately.  Set 0640 permissions (modulo umask).
  //
  // Parameters
  //   path - Path to the file to create
  virtual bool TouchFileDurable(const base::FilePath& path);

  // Delete file(s) at the given path
  //
  // Parameters
  //  path - string containing file path to delete
  //  recursive - whether to perform recursive deletion of the subtree
  virtual bool DeleteFile(const base::FilePath& path, bool recursive);

  // Deletes file durably, i.e. ensuring that the directory entry is immediately
  // removed from the on-disk directory structure.
  //
  // Parameters
  //   path - Path to the file to delete
  virtual bool DeleteFileDurable(const base::FilePath& path, bool recursive);

  // Create a directory with the given path (including parent directories, if
  // missing).  All created directories will have 0700 permissions (modulo
  // umask).
  virtual bool CreateDirectory(const base::FilePath& path);

  // Enumerate all directory entries in a given directory
  //
  // Parameters
  //  path - root of the tree to enumerate
  //  is_recursive - true to enumerate recursively
  //  ent_list - vector of strings to add enumerate directory entry paths into
  virtual bool EnumerateDirectoryEntries(const base::FilePath& path,
                                         bool is_recursive,
                                         std::vector<base::FilePath>* ent_list);

  // Returns a new FileEnumerator instance.
  //
  // The caller TAKES OWNERSHIP of the returned pointer.
  //
  // Parameters
  // (see FileEnumerator())
  virtual FileEnumerator* GetFileEnumerator(const base::FilePath& root_path,
                                            bool recursive,
                                            int file_type);

  // Look up information about a file or directory
  //
  // Parameters
  //  path - element to look up
  //  buf - buffer to store results into
  virtual bool Stat(const base::FilePath& path, struct stat *buf);

  // Return true if |path| has extended attribute |name|, possibly following
  // symlink.
  //
  // Parameters
  //  path - absolute file or directory path to look up
  //  name - name including a namespace prefix. See getxattr(2).
  virtual bool HasExtendedFileAttribute(const base::FilePath& path,
                                        const std::string& name);

  // Add all extended attribute names on |path| to |attr_list|, possibly
  // following symlink.  Returns false on failure.
  //
  // Parameters
  //  path - absolute file or directory path to list attributes for.
  //  attr_list - vector to store attribute names in to.
  virtual bool ListExtendedFileAttributes(const base::FilePath& path,
                                          std::vector<std::string>* attr_list);

  // Return true if extended attribute |name| could be read from |path|,
  // storing the value in |value|, possibly following symlink;
  //
  // Parameters
  //  path - absolute file or directory path to get attributes for
  //  name - name including a namespace prefix. See getxattr(2)
  //  value - variable to store the resulting value in
  virtual bool GetExtendedFileAttributeAsString(const base::FilePath& path,
                                                const std::string& name,
                                                std::string* value);

  // Return true if extended attribute |name| could be read from |path|,
  // storing the value in |value|, possibly following symlink;
  //
  // Parameters
  //  path - absolute file or directory path to get attributes for
  //  name - name including a namespace prefix. See getxattr(2)
  //  value - pointer to store the resulting value in
  //  size - The expected size of the extended attribute.  If this does not
  //  match the actual size false will be returned.
  virtual bool GetExtendedFileAttribute(const base::FilePath& path,
                                        const std::string& name,
                                        char* value,
                                        ssize_t size);

  // Return true if the extended attribute |name| could be set with value
  // |value| on |path|.
  //
  // Parameters
  //  path - absolute file or directory path to set attributes for
  //  name - name including a namespace prefix. See getxattr(2)
  //  value - value to set for the extended attribute
  virtual bool SetExtendedFileAttribute(const base::FilePath& path,
                                        const std::string& name,
                                        const char* value,
                                        size_t size);

  // Return true if the ext file attributes for |name| could be succesfully set
  // in |flags|, possibly following symlink.
  //
  // Parameters
  //  path - absolute file or directory path to get attributes for
  //  flags - the pointer which will store the read flags
  virtual bool GetExtFileAttributes(const base::FilePath& path, int* flags);

  // Return true if the ext file attributes for |name| could be changed to
  // |flags|, possibly following symlink.
  //
  // Parameters
  //  path - absolute file or directory path to get attributes for
  //  flags - the value to update the ext attributes to
  virtual bool SetExtFileAttributes(const base::FilePath& path, int flags);

  // Return if ext file attributes associated with the |name| has FS_NODUMP_FL,
  // possibly following symlink.
  //
  // Parameters
  //  path - absolute file or directory path to look up
  virtual bool HasNoDumpFileAttribute(const base::FilePath& path);

  // Rename a file or directory
  //
  // Parameters
  //  from
  //  to
  virtual bool Rename(const base::FilePath& from, const base::FilePath& to);

  // Returns the current time.
  virtual base::Time GetCurrentTime() const;

  // Copies from to to.
  virtual bool Copy(const base::FilePath& from, const base::FilePath& to);

  // Copies and retains permissions and ownership.
  virtual bool CopyWithPermissions(const base::FilePath& from,
                                   const base::FilePath& to);

  // Moves a given path on the filesystem
  //
  // Parameters
  //   from - path to move
  //   to   - destination of the move
  virtual bool Move(const base::FilePath& from, const base::FilePath& to);

  // Calls statvfs() on path.
  //
  // Parameters
  //   path - path to statvfs on
  //   vfs - buffer to store result in
  virtual bool StatVFS(const base::FilePath& path, struct statvfs* vfs);

  // Find the device for a given filesystem.
  //
  // Parameters
  //   filesystem - the filesystem to examine
  //   device - output: the device name that "filesystem" in mounted on
  virtual bool FindFilesystemDevice(const base::FilePath &filesystem,
                                    std::string *device);

  // Runs "tune2fs -l" with redirected output.
  //
  // Parameters
  //  filesystem - the filesystem to examine
  //  lgofile - the path written with output
  virtual bool ReportFilesystemDetails(const base::FilePath &filesystem,
                                       const base::FilePath &logfile);

  // Sets up a process keyring which links to the user keyring and the session
  // keyring.
  virtual bool SetupProcessKeyring();

  // Returns the state of the directory's encryption key.
  virtual dircrypto::KeyState GetDirCryptoKeyState(const base::FilePath& dir);

  // Sets up a directory to be encrypted with the provided key.
  virtual bool SetDirCryptoKey(const base::FilePath& dir,
                               const brillo::SecureBlob& key_sig);

  // Adds the key to the dircrypto keyring and sets permissions.
  virtual bool AddDirCryptoKeyToKeyring(
      const brillo::SecureBlob& key,
      const brillo::SecureBlob& key_sig,
      key_serial_t* key_id);

  // Invalidates the key to make dircrypto data inaccessible.
  virtual bool InvalidateDirCryptoKey(key_serial_t key_id);

  // Clears the kernel-managed user keyring
  virtual bool ClearUserKeyring();

  // Creates an ecryptfs auth token and installs it in the kernel keyring.
  //
  // Parameters
  //   key - The key to add
  //   key_sig - The key's (ascii) signature
  //   salt - The salt
  virtual bool AddEcryptfsAuthToken(const brillo::SecureBlob& key,
                                    const std::string& key_sig,
                                    const brillo::SecureBlob& salt);

  // Override the location of the mountinfo file used.
  // Default is kMountInfoFile.
  virtual void set_mount_info_path(const base::FilePath &mount_info_path) {
    mount_info_path_ = mount_info_path;
  }

  // Report condition of the Firmware Write-Protect flag.
  virtual bool FirmwareWriteProtected();

  // Syncs file data to disk for the given |path| but syncs metadata only to the
  // extent that is required to acess the file's contents.  (I.e. the directory
  // entry and file size are sync'ed if changed, but not atime or mtime.)  This
  // method is expensive and synchronous, use with care.  Returns true on
  // success.
  virtual bool DataSyncFile(const base::FilePath& path);

  // Syncs file data to disk for the given |path|. All metadata is synced as
  // well as file contents. This method is expensive and synchronous, use with
  // care.  Returns true on success.
  virtual bool SyncFile(const base::FilePath& path);

  // Calls fsync() on directory.  Returns true on success.
  //
  // Parameters
  //   path - Directory to be sync'ed
  virtual bool SyncDirectory(const base::FilePath& path);

  // Syncs everything to disk.  This method is synchronous and very, very
  // expensive; use with even more care than SyncFile.
  virtual void Sync();

  // Gets the system HWID. This is the same ID that is used on some systems to
  // extend the TPM's PCR_1.
  virtual std::string GetHardwareID();

  // Creates a new symbolic link at |path| pointing to |target|.  Returns false
  // if the link could not be created successfully.
  //
  // Parametesr
  //   path - The path to create the symbolic link at.
  //   target - The path that the symbolic link should point to.
  virtual bool CreateSymbolicLink(const base::FilePath& path,
                                  const base::FilePath& target);

  // Reads the target of a symbolic link at |path| into |target|.  If |path| is
  // not a symbolic link or the target cannot be read false is returned.
  //
  // Parameters
  //   path - The path of the symbolic link to read
  //   target - The path to fill with the symbolic link target.  If an error is
  //   encounted and false is returned, target will be cleared.
  virtual bool ReadLink(const base::FilePath& path, base::FilePath* target);

  // Sets the atime and mtime of a file to the provided values, optionally
  // following symlinks.
  //
  // Parameters
  //   path - The path of the file to set times for.  If this is a relative
  //   path, it is interpreted as relative to the current working directory of
  //   the calling process.
  //   atime - The time to set as the file's last access time.
  //   mtime - The time to set as the file's last modified time.
  //   follow_links - If set to true, symlinks will be dereferenced and their
  //   targets will be updated.
  virtual bool SetFileTimes(const base::FilePath& path,
                            const struct timespec& atime,
                            const struct timespec& mtime,
                            bool follow_links);

  // Copies |count| bytes of data from |from| to |to|, starting at |offset| in
  // |from| and the current file offset in |to|.  If
  // the copy fails or is only partially successful (bytes written does not
  // equal |count|) false is returned.
  //
  // Parameters
  //   to - The file to copy data to.
  //   from - The file to copy data from.
  //   offset - The location in |from| to begin copying data from.  This has no
  //   impact on the location in |to| that data is written to.
  //   count - The number of bytes to copy.
  virtual bool SendFile(const base::File& to,
                        const base::File& from,
                        off_t offset,
                        size_t count);

 private:
  // Returns the process and open file information for the specified process id
  // with files open on the given path
  //
  // Parameters
  //   pid - The process to check
  //   path_in - The file path to check for
  //   process_info (OUT) - The ProcessInformation to store the results in
  void GetProcessOpenFileInformation(pid_t pid, const base::FilePath& path_in,
                                     ProcessInformation* process_info);

  // Returns a vector of PIDs that have files open on the given path
  //
  // Parameters
  //   path - The path to check if the process has open files on
  //   pids (OUT) - The PIDs found
  void LookForOpenFiles(const base::FilePath& path, std::vector<pid_t>* pids);

  // Returns true if child is a file or folder below or equal to parent.  If
  // parent is a directory, it should end with a '/' character.
  //
  // Parameters
  //   parent - The parent directory
  //   child - The child directory/file
  bool IsPathChild(const base::FilePath& parent, const base::FilePath& child);

  // Creates a random string suitable to append to a filename.  Returns empty
  // string in case of error.
  virtual std::string GetRandomSuffix();

  // Calls fdatasync() on file if data_sync is true or fsync() on directory or
  // file when data_sync is false.  Returns true on success.
  //
  // Parameters
  //   path - File/directory to be sync'ed
  //   is_directory - True if |path| is a directory
  //   data_sync - True if |path| does not need metadata to be synced
  bool SyncFileOrDirectory(const base::FilePath& path,
                           bool is_directory,
                           bool data_sync);

  // Calls |callback| with |path| and, if |path| is a directory, with every
  // entry recursively.  Order is not guaranteed, see base::FileEnumerator.  If
  // |path| is an absolute path, then the file names sent to |callback| will
  // also be absolute.  Returns true if all invocations of |callback| succeed.
  // If an invocation fails, the walk terminates and false is returned.
  bool WalkPath(const base::FilePath& path,
                const FileEnumeratorCallback& callback);

  // Copies permissions from a file specified by |file_path| and |file_info| to
  // another file with the same name but a child of |new_base|, not |old_base|.
  bool CopyPermissionsCallback(const base::FilePath& old_base,
                               const base::FilePath& new_base,
                               const base::FilePath& file_path,
                               const struct stat& file_info);

  // Applies ownership and permissions to a single file or directory.
  //
  // Parameters
  //   default_file_info - Default ownership / perms for files.
  //   default_dir_info - Default ownership / perms for directories.
  //   special_cases - A map of absolute path to ownership / perms.
  //   file_info - Info about the file or directory.
  bool ApplyPermissionsCallback(
      const Permissions& default_file_info,
      const Permissions& default_dir_info,
      const std::map<base::FilePath, Permissions>& special_cases,
      const base::FilePath& file_path,
      const struct stat& file_info);

  void PostWorkerTask(const base::Closure& task);

  // Computes a checksum and returns an ASCII representation.
  std::string GetChecksum(const void* input, size_t input_size);

  // Computes a checksum of |content| and writes it atomically to the same
  // |path| but with a .sum suffix and the given |mode|.
  void WriteChecksum(const base::FilePath& path,
                     const void* content,
                     size_t content_size,
                     mode_t mode);

  // Looks for a .sum file for |path| and verifies the checksum if it exists.
  void VerifyChecksum(const base::FilePath& path,
                      const void* content,
                      size_t content_size);

  bool DecodeProcInfoLine(const std::string& line,
                          std::vector<std::string>* args,
                          size_t* file_system_type_idx);

  base::FilePath mount_info_path_;

  friend class PlatformTest;
  FRIEND_TEST(PlatformTest, DecodeProcInfoLineCorruptedMountInfo);
  FRIEND_TEST(PlatformTest, DecodeProcInfoLineIncompleteMountInfo);
  FRIEND_TEST(PlatformTest, DecodeProcInfoLineGood);
  DISALLOW_COPY_AND_ASSIGN(Platform);
};

class ProcessInformation {
 public:
  ProcessInformation()
      : cmd_line_(),
      process_id_(-1) { }
  virtual ~ProcessInformation() { }

  std::string GetCommandLine() const {
    std::string result;
    for (const auto& cmd : cmd_line_) {
      if (result.length() != 0) {
        result.append(" ");
      }
      result.append(cmd);
    }
    return result;
  }

  // Set the command line array.  This method DOES swap out the contents of
  // |value|.  The caller should expect an empty vector on return.
  void set_cmd_line(std::vector<std::string>* value) {
    cmd_line_.clear();
    cmd_line_.swap(*value);
  }

  const std::vector<std::string>& get_cmd_line() const {
    return cmd_line_;
  }

  // Set the open file array.  This method DOES swap out the contents of
  // |value|.  The caller should expect an empty set on return.
  void set_open_files(std::set<base::FilePath>* value) {
    open_files_.clear();
    open_files_.swap(*value);
  }

  const std::set<base::FilePath>& get_open_files() const {
    return open_files_;
  }

  // Set the command line array.  This method DOES swap out the contents of
  // |value|.  The caller should expect an empty string on return.
  void set_cwd(std::string* value) {
    cwd_.clear();
    cwd_.swap(*value);
  }

  const std::string& get_cwd() const {
    return cwd_;
  }

  void set_process_id(int value) {
    process_id_ = value;
  }

  int get_process_id() const {
    return process_id_;
  }

 private:
  std::vector<std::string> cmd_line_;
  std::set<base::FilePath> open_files_;
  std::string cwd_;
  int process_id_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PLATFORM_H_
