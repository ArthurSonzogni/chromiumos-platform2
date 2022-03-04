// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package genutil provides utility functions for generators to generate a part of the output string.
package genutil

import (
	"strings"
	"unicode"
)

// GenerateHeaderGuard generates a string of a header guard.
func GenerateHeaderGuard(path string) string {
	s := "____chromeos_dbus_binding__" + path
	mapping := func(r rune) rune {
		switch {
		case unicode.IsLetter(r):
			return unicode.ToUpper(r)
		case unicode.IsDigit(r):
			return r
		default:
			return '_'
		}
	}
	return strings.Map(mapping, s)
}

// MakeInterfaceName makes a name of the class defining the interface.
func MakeInterfaceName(introspectItfName string) string {
	s := strings.Split(introspectItfName, ".")
	return s[len(s)-1] + "Interface"
}

// MakeAdaptorName makes a name of the class serving as a adaptor.
func MakeAdaptorName(introspectItfName string) string {
	s := strings.Split(introspectItfName, ".")
	return s[len(s)-1] + "Adaptor"
}

// MakeFullItfName makes a full name of interface in C++ style.
func MakeFullItfName(introspectItfName string) string {
	return strings.Replace(introspectItfName, ".", "::", -1)
}

// ExtractNameSpaces extract the namespace parts of the interface from the interface name.
func ExtractNameSpaces(introspectItfName string) []string {
	s := strings.Split(introspectItfName, ".")
	return s[:len(s)-1]
}

// Reverse overwrites the slice in reverse order.
func Reverse(s []string) []string {
	for i, j := 0, len(s)-1; i < j; i, j = i+1, j-1 {
		s[i], s[j] = s[j], s[i]
	}
	return s
}
