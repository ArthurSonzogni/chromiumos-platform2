// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package main implements the generate-chromeos-dbus-bindings used to generate
// dbus bindings
package main

import (
	"fmt"
	"io/ioutil"
	"log"
	"os"

	"chromiumos/dbusbindings/introspect"
)

func main() {
	for _, path := range os.Args[1:] {
		b, err := ioutil.ReadFile(path)
		if err != nil {
			log.Fatalf("Failed to read file %s: %v\n", path, err)
		}

		introspection, err := introspect.Parse(b)
		if err != nil {
			log.Fatalf("Failed to parse interface file %s: %v\n", path, err)
		}
		fmt.Printf("%+v\n", introspection)
	}
}
