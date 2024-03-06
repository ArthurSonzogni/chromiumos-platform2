// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::fs::create_dir;
use std::io::Read;
use std::os::unix::fs::symlink;
use std::{fs::File, os::fd::AsRawFd};

use libc::{fcntl, F_GETFL, O_CREAT, O_DIRECTORY, O_NONBLOCK, O_RDONLY, O_RDWR};
use tempfile::{NamedTempFile, TempDir};

use crate::open_safely;

#[test]
fn open_nonexistent_file() {
    open_safely("/this/path/does/not/exist", O_RDONLY, 0o644)
        .expect_err("opening a nonexistent path without O_CREAT should fail");
}

#[test]
fn open_nonexistent_dir() {
    open_safely("/this/path/does/not/exist", O_DIRECTORY | O_RDONLY, 0)
        .expect_err("opening a nonexistent directory should fail");
}

#[test]
fn open_relative() {
    open_safely("this/path/is/relative", O_RDWR | O_CREAT, 0o644)
        .expect_err("open_safely with a relative path should fail");
}

#[test]
fn open_cwd_relative() {
    open_safely("./this/path/is/relative", O_RDWR | O_CREAT, 0o644)
        .expect_err("open_safely with a relative path should fail");
}

#[test]
fn open_parent_dir_relative() {
    open_safely("../this/path/is/relative", O_RDWR | O_CREAT, 0o644)
        .expect_err("open_safely with a relative path should fail");
}

#[test]
fn open_file() {
    let temp_file = NamedTempFile::new().unwrap();
    let _fd = open_safely(temp_file.path(), O_RDONLY, 0).unwrap();
}

#[test]
fn open_file_no_nonblock() {
    let temp_file = NamedTempFile::new().unwrap();
    let fd = open_safely(temp_file.path(), O_RDONLY, 0).unwrap();
    let flags = unsafe { fcntl(fd.as_raw_fd(), F_GETFL) };
    assert_ne!(flags, -1);
    assert_eq!(flags & O_NONBLOCK, 0);
}

#[test]
fn open_file_as_dir() {
    let temp_file = NamedTempFile::new().unwrap();
    open_safely(temp_file.path(), O_DIRECTORY | O_RDONLY, 0)
        .expect_err("opening a file with O_DIRECTORY should fail");
}

#[test]
fn open_dir() {
    let temp_dir = TempDir::new().unwrap();
    let _fd = open_safely(temp_dir.path(), O_DIRECTORY | O_RDONLY, 0).unwrap();
}

#[test]
fn open_dir_as_file() {
    let temp_dir = TempDir::new().unwrap();
    open_safely(temp_dir.path(), O_RDONLY, 0o644)
        .expect_err("opening a directory without O_DIRECTORY should fail");
}

#[test]
fn open_symlink_final() {
    let temp_dir = TempDir::new().unwrap();

    // temp_dir/test.txt
    let test_txt_path = temp_dir.path().join("test.txt");
    std::fs::write(&test_txt_path, "hello").unwrap();

    // temp_dir/symlink.txt -> test.txt
    let symlink_path = temp_dir.path().join("symlink.txt");
    symlink(&test_txt_path, &symlink_path).unwrap();

    // Opening the symlink should fail.
    open_safely(symlink_path, O_RDONLY, 0o644).expect_err("opening a symlink should fail");

    // Opening the file itself should succeed.
    let fd = open_safely(test_txt_path, O_RDONLY, 0o644).unwrap();
    let mut file = File::from(fd);
    let mut data = String::new();
    file.read_to_string(&mut data).unwrap();
    assert_eq!(data, "hello");
}

#[test]
fn open_symlink_nonfinal() {
    let temp_dir = TempDir::new().unwrap();

    // temp_dir/mydir/
    let mydir_path = temp_dir.path().join("mydir");
    create_dir(&mydir_path).unwrap();

    // temp_dir/mydir/test.txt
    let test_txt_path = mydir_path.join("test.txt");
    std::fs::write(&test_txt_path, "hello").unwrap();

    // temp_dir/symlink -> mydir
    let symlink_path = temp_dir.path().join("symlink");
    symlink(&mydir_path, &symlink_path).unwrap();

    // Opening temp_dir/symlink/test.txt should fail due to the symlink in the middle.
    open_safely(symlink_path.join("test.txt"), O_RDONLY, 0o644)
        .expect_err("opening a symlink should fail");

    // Opening temp_dir/my_dir/test.txt should succeed.
    let fd = open_safely(test_txt_path, O_RDONLY, 0o644).unwrap();
    let mut file = File::from(fd);
    let mut data = String::new();
    file.read_to_string(&mut data).unwrap();
    assert_eq!(data, "hello");
}
