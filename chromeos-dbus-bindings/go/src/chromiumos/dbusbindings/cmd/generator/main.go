// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package main implements the generate-chromeos-dbus-bindings used to generate
// dbus bindings
package main

import (
	"flag"
	"io/ioutil"
	"log"
	"os"

	"chromiumos/dbusbindings/generate/methodnames"
	"chromiumos/dbusbindings/introspect"
)

func main() {
	methodNamesFilePath := flag.String("method-names", "", "the output header file with string constants for each method name")
	flag.Parse()

	var introspections []introspect.Introspection
	for _, path := range flag.Args() {
		b, err := ioutil.ReadFile(path)
		if err != nil {
			log.Fatalf("Failed to read file %s: %v\n", path, err)
		}

		introspection, err := introspect.Parse(b)
		if err != nil {
			log.Fatalf("Failed to parse interface file %s: %v\n", path, err)
		}

		introspections = append(introspections, introspection)
	}

	if *methodNamesFilePath != "" {
		f, err := os.Create(*methodNamesFilePath)
		if err != nil {
			log.Fatalf("Failed to create file %s: %v\n", *methodNamesFilePath, err)
		}
		defer func() {
			if err := f.Close(); err != nil {
				log.Fatalf("Failed to close file %s: %v\n", *methodNamesFilePath, err)
			}
		}()

		if err := methodnames.Generate(introspections, f); err != nil {
			log.Fatalf("Failed to generate methodnames: %v\n", err)
		}
	}
}
