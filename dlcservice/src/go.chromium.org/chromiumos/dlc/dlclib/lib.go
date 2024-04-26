// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package dlclib implements functions dealing with dlcservice.
package dlclib

import (
	"crypto/sha256"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"syscall"
)

// Init is a placeholder function.
func Init() {
}

// IsWritable checks if the given path is writable.
func IsWritable(p string) bool {
	return syscall.Access(p, 0x2) == nil
}

// PathExists checks if the given path exists.
func PathExists(p string) (bool, error) {
	_, err := os.Stat(p)
	if err == nil {
		return true, nil
	}
	if os.IsNotExist(err) {
		return false, nil
	}
	return false, err
}

// GetFileSizeInBlocks returns the file size in 4KiB blocks.
func GetFileSizeInBlocks(p string) (int64, error) {
	info, err := os.Stat(p)
	if err != nil {
		return 0, err
	}

	size := info.Size()
	bs := int64(4096)
	b := int64(math.Ceil(float64(size) / float64(bs)))

	return b, nil
}

// AppendFile will append `src` contents of a file to `dst` file.
func AppendFile(src, dst string) error {
	return srcToDst(src, dst, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
}

// CopyFile will copy `src` contents of a file to `dst` file.
func CopyFile(src, dst string) error {
	return srcToDst(src, dst, os.O_TRUNC|os.O_CREATE|os.O_WRONLY, 0644)
}

// CopyDirectory will copy all files in `src` into `dst`.
func CopyDirectory(src, dst string) error {
	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		if path == src {
			return nil
		}

		relPath, err := filepath.Rel(src, path)
		if err != nil {
			return err
		}
		dstPath := filepath.Join(dst, relPath)

		if info.IsDir() {
			return os.Mkdir(dstPath, info.Mode())
		}
		return CopyFile(path, dstPath)
	})
}

// DirectorySize will return the bytes used within a directory (file sizes + directory as 4KiB).
func DirectorySize(p string) (int64, error) {
	var size int64

	err := filepath.Walk(p, func(_ string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() {
			size += info.Size()
		} else {
			size += 4096
		}
		return nil
	})

	return size, err
}

// Sha256Sum will return the hex sha256sum of a given file.
func Sha256Sum(p string) (string, error) {
	var err error
	var file *os.File

	if file, err = os.Open(p); err != nil {
		return "", err
	}
	defer file.Close()

	hash := sha256.New()

	if _, err = io.Copy(hash, file); err != nil {
		return "", err
	}

	return fmt.Sprintf("%x", hash.Sum(nil)), nil
}
