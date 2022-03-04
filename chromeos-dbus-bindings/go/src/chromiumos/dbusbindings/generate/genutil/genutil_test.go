// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package genutil_test

import (
	"testing"

	"chromiumos/dbusbindings/generate/genutil"

	"github.com/google/go-cmp/cmp"
)

func TestGenerateHeaderGuard(t *testing.T) {
	got := genutil.GenerateHeaderGuard("/foo/bar3_BAZ/adaptor.h")
	want := "____CHROMEOS_DBUS_BINDING___FOO_BAR3_BAZ_ADAPTOR_H"
	if diff := cmp.Diff(got, want); diff != "" {
		t.Errorf("GenerateHeaderGuard diff (-got +want):\n%s", diff)
	}
}

func TestMakeInterfaceName(t *testing.T) {
	got := genutil.MakeInterfaceName("foo.bar.BazQux")
	want := "BazQuxInterface"
	if diff := cmp.Diff(got, want); diff != "" {
		t.Errorf("MakeInterfaceName diff (-got +want):\n%s", diff)
	}
}

func TestMakeAdaptorName(t *testing.T) {
	got := genutil.MakeAdaptorName("foo.bar.BazQux")
	want := "BazQuxAdaptor"
	if diff := cmp.Diff(got, want); diff != "" {
		t.Errorf("MakeAdaptorName diff (-got +want):\n%s", diff)
	}
}

func TestMakeFullItfName(t *testing.T) {
	got := genutil.MakeFullItfName("foo.bar.BazQux")
	want := "foo::bar::BazQux"
	if diff := cmp.Diff(got, want); diff != "" {
		t.Errorf("MakeFullItfName diff (-got +want):\n%s", diff)
	}
}

func TestExtractNameSpaces(t *testing.T) {
	cases := []struct {
		input string
		want  []string
	}{
		{input: "foo", want: []string{}},
		{input: "foo.bar.BazQux", want: []string{"foo", "bar"}},
	}

	for _, tc := range cases {
		got := genutil.ExtractNameSpaces(tc.input)
		if diff := cmp.Diff(got, tc.want); diff != "" {
			t.Errorf("Wrong result in ExtractNameSpaces(%q): diff (-got +want):\n%s", tc.input, diff)
		}
	}
}

func TestReverse(t *testing.T) {
	cases := []struct {
		input, want []string
	}{
		{input: []string{}, want: []string{}},
		{input: []string{"foo"}, want: []string{"foo"}},
		{input: []string{"foo", "bar"}, want: []string{"bar", "foo"}},
	}

	for _, tc := range cases {
		got := genutil.Reverse(tc.input)
		if diff := cmp.Diff(got, tc.want); diff != "" {
			t.Errorf("Wrong result in Reverse(%q): diff (-got +want):\n%s", tc.input, diff)
		}
	}
}
