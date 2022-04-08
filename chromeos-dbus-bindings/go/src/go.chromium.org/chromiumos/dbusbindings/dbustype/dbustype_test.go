// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dbustype_test

import (
	"testing"

	"go.chromium.org/chromiumos/dbusbindings/dbustype"

	"github.com/google/go-cmp/cmp"
)

func TestParseFailures(t *testing.T) {
	cases := []string{
		"a{sv}Garbage", "", "a", "a{}", "a{s}", "a{sa}i", "a{s", "al", "(l)", "(i",
		"^MyProtobufClass", "a{s{i}}", "a{sa{i}u}", "a{a{u}", "a}i{",
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaai",
		"(((((((((((((((((((((((((((((((((i)))))))))))))))))))))))))))))))))",
	}
	for _, tc := range cases {
		if _, err := dbustype.Parse(tc); err == nil {
			t.Errorf("Expected signature %s to fail but it succeeded", tc)
		}
	}
}

func TestParseSuccesses(t *testing.T) {
	cases := []struct {
		input string
		want  string
	}{
		// Simple types.
		{"b", "bool"},
		{"y", "uint8_t"},
		{"d", "double"},
		{"o", "dbus::ObjectPath"},
		{"n", "int16_t"},
		{"i", "int32_t"},
		{"x", "int64_t"},
		{"s", "std::string"},
		{"q", "uint16_t"},
		{"u", "uint32_t"},
		{"t", "uint64_t"},
		{"v", "brillo::Any"},

		// Complex types.
		{"ab", "std::vector<bool>"},
		{"ay", "std::vector<uint8_t>"},
		{"aay", "std::vector<std::vector<uint8_t>>"},
		{"ao", "std::vector<dbus::ObjectPath>"},
		{"a{oa{sa{sv}}}",
			"std::map<dbus::ObjectPath, std::map<std::string, brillo::VariantDictionary>>"},
		{"a{os}", "std::map<dbus::ObjectPath, std::string>"},
		{"as", "std::vector<std::string>"},
		{"a{ss}", "std::map<std::string, std::string>"},
		{"a{sa{ss}}",
			"std::map<std::string, std::map<std::string, std::string>>"},
		{"a{sa{sv}}", "std::map<std::string, brillo::VariantDictionary>"},
		{"a{sv}", "brillo::VariantDictionary"},
		{"at", "std::vector<uint64_t>"},
		{"a{iv}", "std::map<int32_t, brillo::Any>"},
		{"(ib)", "std::tuple<int32_t, bool>"},
		{"(ibs)", "std::tuple<int32_t, bool, std::string>"},
		{"((i))", "std::tuple<std::tuple<int32_t>>"},
	}

	for _, tc := range cases {
		typ, err := dbustype.Parse(tc.input)
		if err != nil {
			t.Fatalf("Parse(%q) got error, want nil: %v", tc.input, err)
		}
		got := typ.BaseType(dbustype.DirectionExtract)
		if diff := cmp.Diff(got, tc.want); diff != "" {
			t.Errorf("getting the base type of %q failed\ndirection: DirectionExtract\n(-got +want):\n%s", tc.input, diff)
		}
		got = typ.BaseType(dbustype.DirectionAppend)
		if diff := cmp.Diff(got, tc.want); diff != "" {
			t.Errorf("getting the base type of %q failed\ndirection: DirectionAppend\n(-got +want):\n%s", tc.input, diff)
		}
	}

	manyNestedCases := []string{
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaai",
		"((((((((((((((((((((((((((((((((i))))))))))))))))))))))))))))))))",
	}
	for _, tc := range manyNestedCases {
		if _, err := dbustype.Parse(tc); err != nil {
			t.Fatalf("Parse(%q) got error, want nil: %v", tc, err)
		}
	}
}

// Scalar types should not have reference behavior when used as in-args, and
// should just produce the base type as their in-arg type.
func TestInArgScalarTypes(t *testing.T) {
	cases := []string{
		"b", "y", "d", "n", "i", "x", "q", "u", "t",
	}
	for _, tc := range cases {
		typ, err := dbustype.Parse(tc)
		if err != nil {
			t.Fatalf("Parse(%q) got error, want nil: %v", tc, err)
		}
		got := typ.InArgType(dbustype.ReceiverAdaptor)
		want := typ.BaseType(dbustype.DirectionExtract)
		if got != want {
			t.Fatalf("typ.BaseType(DirectionExtract) and typ.InArgType(ReceiverAdaptor) mismatch, typ is %q", tc)
		}
		got = typ.InArgType(dbustype.ReceiverProxy)
		want = typ.BaseType(dbustype.DirectionAppend)
		if got != want {
			t.Fatalf("typ.BaseType(DirectionAppend) and typ.InArgType(ReceiverProxy) mismatch, typ is %q", tc)
		}
	}
}

// Non-scalar types should have const reference behavior when used as in-args.
// The references should not be nested.
func TestInArgNonScalarTypes(t *testing.T) {
	cases := []struct {
		input string
		want  string
	}{
		{"o", "const dbus::ObjectPath&"},
		{"s", "const std::string&"},
		{"v", "const brillo::Any&"},
		{"ab", "const std::vector<bool>&"},
		{"ay", "const std::vector<uint8_t>&"},
		{"aay", "const std::vector<std::vector<uint8_t>>&"},
		{"ao", "const std::vector<dbus::ObjectPath>&"},
		{"a{oa{sa{sv}}}",
			"const std::map<dbus::ObjectPath, std::map<std::string, brillo::VariantDictionary>>&"},
		{"a{os}", "const std::map<dbus::ObjectPath, std::string>&"},
		{"as", "const std::vector<std::string>&"},
		{"a{ss}", "const std::map<std::string, std::string>&"},
		{"a{sa{ss}}",
			"const std::map<std::string, std::map<std::string, std::string>>&"},
		{"a{sa{sv}}",
			"const std::map<std::string, brillo::VariantDictionary>&"},
		{"a{sv}", "const brillo::VariantDictionary&"},
		{"at", "const std::vector<uint64_t>&"},
		{"a{iv}", "const std::map<int32_t, brillo::Any>&"},
		{"(ib)", "const std::tuple<int32_t, bool>&"},
		{"(ibs)", "const std::tuple<int32_t, bool, std::string>&"},
		{"((i))", "const std::tuple<std::tuple<int32_t>>&"},
	}

	for _, tc := range cases {
		typ, err := dbustype.Parse(tc.input)
		if err != nil {
			t.Fatalf("Parse(%q) got error, want nil: %v", tc.input, err)
		}
		got := typ.InArgType(dbustype.ReceiverAdaptor)
		if diff := cmp.Diff(got, tc.want); diff != "" {
			t.Errorf("getting the in arg type of %q failed\nreceiver: ReceiverAdaptor\n(-got +want):\n%s", tc.input, diff)
		}
		got = typ.InArgType(dbustype.ReceiverProxy)
		if diff := cmp.Diff(got, tc.want); diff != "" {
			t.Errorf("getting the in arg type of %q failed\nreceiver: ReceiverProxy\n(-got +want):\n%s", tc.input, diff)
		}
	}
}

// Out-args should be pointers, but only at the top level.
func TestOutArgTypes(t *testing.T) {
	cases := []struct {
		input string
		want  string
	}{
		{"b", "bool*"},
		{"y", "uint8_t*"},
		{"i", "int32_t*"},
		{"t", "uint64_t*"},
		{"o", "dbus::ObjectPath*"},
		{"s", "std::string*"},
		{"v", "brillo::Any*"},
		{"ab", "std::vector<bool>*"},
		{"ay", "std::vector<uint8_t>*"},
		{"aay", "std::vector<std::vector<uint8_t>>*"},
		{"ao", "std::vector<dbus::ObjectPath>*"},
		{"a{oa{sa{sv}}}",
			"std::map<dbus::ObjectPath, std::map<std::string, brillo::VariantDictionary>>*"},
		{"a{os}", "std::map<dbus::ObjectPath, std::string>*"},
		{"as", "std::vector<std::string>*"},
		{"a{ss}", "std::map<std::string, std::string>*"},
		{"a{sa{ss}}",
			"std::map<std::string, std::map<std::string, std::string>>*"},
		{"a{sa{sv}}",
			"std::map<std::string, brillo::VariantDictionary>*"},
		{"a{sv}", "brillo::VariantDictionary*"},
		{"at", "std::vector<uint64_t>*"},
		{"a{iv}", "std::map<int32_t, brillo::Any>*"},
		{"(ib)", "std::tuple<int32_t, bool>*"},
		{"(ibs)", "std::tuple<int32_t, bool, std::string>*"},
		{"((i))", "std::tuple<std::tuple<int32_t>>*"},
	}

	for _, tc := range cases {
		typ, err := dbustype.Parse(tc.input)
		if err != nil {
			t.Fatalf("Parse(%q) got error, want nil: %v", tc.input, err)
		}
		got := typ.OutArgType(dbustype.ReceiverAdaptor)
		if diff := cmp.Diff(got, tc.want); diff != "" {
			t.Errorf("getting the out arg type of %q failed\nreceiver: ReceiverAdaptor\n(-got +want):\n%s", tc.input, diff)
		}
		got = typ.OutArgType(dbustype.ReceiverProxy)
		if diff := cmp.Diff(got, tc.want); diff != "" {
			t.Errorf("getting the out arg type of %q failed\nreceiver: ReceiverProxy\n(-got +want):\n%s", tc.input, diff)
		}
	}
}

// TODO(chromium:983008): Add tests for PropertyType.

// For file descriptors type, it matters whether DirectionExtract or DirectionAppend.
func TestFileDescriptors(t *testing.T) {
	cases := []struct {
		input             string
		BaseTypeExtract   string
		BaseTypeAppend    string
		InArgTypeAdaptor  string
		InArgTypeProxy    string
		OutArgTypeAdaptor string
		OutArgTypeProxy   string
	}{
		{
			input:             "h",
			BaseTypeExtract:   "base::ScopedFD",
			BaseTypeAppend:    "brillo::dbus_utils::FileDescriptor",
			InArgTypeAdaptor:  "const base::ScopedFD&",
			InArgTypeProxy:    "const brillo::dbus_utils::FileDescriptor&",
			OutArgTypeAdaptor: "brillo::dbus_utils::FileDescriptor*",
			OutArgTypeProxy:   "base::ScopedFD*",
		},
		// Check that more involved types are correct as well.
		{
			input:           "ah",
			BaseTypeExtract: "std::vector<base::ScopedFD>",
			BaseTypeAppend:  "std::vector<brillo::dbus_utils::FileDescriptor>",
		}, {
			input:           "a{ih}",
			BaseTypeExtract: "std::map<int32_t, base::ScopedFD>",
			BaseTypeAppend:  "std::map<int32_t, brillo::dbus_utils::FileDescriptor>",
		}, {
			input:           "(ih)",
			BaseTypeExtract: "std::tuple<int32_t, base::ScopedFD>",
			BaseTypeAppend:  "std::tuple<int32_t, brillo::dbus_utils::FileDescriptor>",
		},
	}

	for _, tc := range cases {
		typ, err := dbustype.Parse(tc.input)
		if err != nil {
			t.Fatalf("Parse(%q) got error, want nil: %v", tc.input, err)
		}
		got := typ.BaseType(dbustype.DirectionExtract)
		if diff := cmp.Diff(got, tc.BaseTypeExtract); diff != "" {
			t.Errorf("getting the base type of %q failed\ndirection: DirectionExtract\n(-got +want):\n%s", tc.input, diff)
		}
		got = typ.BaseType(dbustype.DirectionAppend)
		if diff := cmp.Diff(got, tc.BaseTypeAppend); diff != "" {
			t.Errorf("getting the base type of %q failed\ndirection: DirectionAppend\n(-got +want):\n%s", tc.input, diff)
		}

		if tc.InArgTypeAdaptor != "" {
			got = typ.InArgType(dbustype.ReceiverAdaptor)
			if diff := cmp.Diff(got, tc.InArgTypeAdaptor); diff != "" {
				t.Errorf("getting the in arg type of %q failed\nreceiver: ReceiverAdaptor\n(-got +want):\n%s", tc.input, diff)
			}
		}
		if tc.InArgTypeProxy != "" {
			got = typ.InArgType(dbustype.ReceiverProxy)
			if diff := cmp.Diff(got, tc.InArgTypeProxy); diff != "" {
				t.Errorf("getting the in arg type of %q failed\nreceiver: ReceiverProxy\n(-got +want):\n%s", tc.input, diff)
			}
		}
		if tc.OutArgTypeAdaptor != "" {
			got = typ.OutArgType(dbustype.ReceiverAdaptor)
			if diff := cmp.Diff(got, tc.OutArgTypeAdaptor); diff != "" {
				t.Errorf("getting the out arg type of %q failed\nreceiver: ReceiverAdaptor\n(-got +want):\n%s", tc.input, diff)
			}
		}
		if tc.OutArgTypeProxy != "" {
			got = typ.OutArgType(dbustype.ReceiverProxy)
			if diff := cmp.Diff(got, tc.OutArgTypeProxy); diff != "" {
				t.Errorf("getting the out arg type of %q failed\nreceiver: ReceiverProxy\n(-got +want):\n%s", tc.input, diff)
			}
		}
	}
}
