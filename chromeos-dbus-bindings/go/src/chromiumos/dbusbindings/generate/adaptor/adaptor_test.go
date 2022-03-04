// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package adaptor_test

import (
	"bytes"
	"testing"

	"chromiumos/dbusbindings/generate/adaptor"
	"chromiumos/dbusbindings/introspect"

	"github.com/google/go-cmp/cmp"
)

const (
	generateAdaptorsOutput = `// Automatic generation of D-Bus interfaces:
//  - org.chromium.Test
//  - org.chromium.Test2
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR_H
#define ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR_H
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/scoped_file.h>
#include <dbus/object_path.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/variant_dictionary.h>

namespace org {
namespace chromium {

// Interface definition for org::chromium::Test.
class TestInterface {
 public:
  virtual ~TestInterface() = default;
};

// Interface adaptor for org::chromium::Test.
class TestAdaptor {
 public:
  TestAdaptor(TestInterface* interface) : interface_(interface) {}
  TestAdaptor(const TestAdaptor&) = delete;
  TestAdaptor& operator=(const TestAdaptor&) = delete;

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("org.chromium.Test");
  }

  static dbus::ObjectPath GetObjectPath() {
    return dbus::ObjectPath{"/org/chromium/Test"};
  }

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"org.chromium.Test\">\n"
        "  </interface>\n";
  }

 private:
  TestInterface* interface_;  // Owned by container of this adapter.
};

}  // namespace chromium
}  // namespace org

namespace org {
namespace chromium {

// Interface definition for org::chromium::Test2.
class Test2Interface {
 public:
  virtual ~Test2Interface() = default;
};

// Interface adaptor for org::chromium::Test2.
class Test2Adaptor {
 public:
  Test2Adaptor(Test2Interface* /* interface */) {}
  Test2Adaptor(const Test2Adaptor&) = delete;
  Test2Adaptor& operator=(const Test2Adaptor&) = delete;

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("org.chromium.Test2");
  }

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"org.chromium.Test2\">\n"
        "  </interface>\n";
  }

 private:
};

}  // namespace chromium
}  // namespace org


// Interface definition for EmptyInterface.
class EmptyInterfaceInterface {
 public:
  virtual ~EmptyInterfaceInterface() = default;
};

// Interface adaptor for EmptyInterface.
class EmptyInterfaceAdaptor {
 public:
  EmptyInterfaceAdaptor(EmptyInterfaceInterface* /* interface */) {}
  EmptyInterfaceAdaptor(const EmptyInterfaceAdaptor&) = delete;
  EmptyInterfaceAdaptor& operator=(const EmptyInterfaceAdaptor&) = delete;

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("EmptyInterface");
  }

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"EmptyInterface\">\n"
        "  </interface>\n";
  }

 private:
};

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR_H
`

	newFileDescriptorsOutput = `// Automatic generation of D-Bus interfaces:
//  - org.chromium.Test
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR2_H
#define ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR2_H
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/scoped_file.h>
#include <dbus/object_path.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/variant_dictionary.h>

namespace org {
namespace chromium {

// Interface definition for org::chromium::Test.
class TestInterface {
 public:
  virtual ~TestInterface() = default;
};

// Interface adaptor for org::chromium::Test.
class TestAdaptor {
 public:
  TestAdaptor(TestInterface* /* interface */) {}
  TestAdaptor(const TestAdaptor&) = delete;
  TestAdaptor& operator=(const TestAdaptor&) = delete;

  void RegisterWithDBusObject(brillo::dbus_utils::DBusObject* object) {
    brillo::dbus_utils::DBusInterface* itf =
        object->AddOrGetInterface("org.chromium.Test");
  }

  static dbus::ObjectPath GetObjectPath() {
    return dbus::ObjectPath{"/org/chromium/Test"};
  }

  static const char* GetIntrospectionXml() {
    return
        "  <interface name=\"org.chromium.Test\">\n"
        "  </interface>\n";
  }

 private:
};

}  // namespace chromium
}  // namespace org
#endif  // ____CHROMEOS_DBUS_BINDING___TMP_ADAPTOR2_H
`
)

func TestGenerateAdaptors(t *testing.T) {
	itf := introspect.Interface{
		Name: "org.chromium.Test",
		Methods: []introspect.Method{
			{
				Name: "Kaneda",
			},
		},
	}

	itf2 := introspect.Interface{
		Name: "org.chromium.Test2",
	}

	itf3 := introspect.Interface{
		Name: "EmptyInterface",
	}

	introspections := []introspect.Introspection{
		{
			Name:       "/org/chromium/Test",
			Interfaces: []introspect.Interface{itf},
		}, {
			Interfaces: []introspect.Interface{itf2},
		}, {
			Interfaces: []introspect.Interface{itf3},
		},
	}

	out := new(bytes.Buffer)
	if err := adaptor.Generate(introspections, out, "/tmp/adaptor.h"); err != nil {
		t.Errorf("Generate got error, want nil: %v", err)
	}

	if diff := cmp.Diff(out.String(), generateAdaptorsOutput); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

// Note that new-style FD bindings use brillo::dbus_utils::FileDescriptor and base::ScopedFD.
func TestGenerateAdaptorIncludingNewFileDescriptors(t *testing.T) {
	itf := introspect.Interface{
		Name: "org.chromium.Test",
	}

	introspections := []introspect.Introspection{
		{
			Name:       "/org/chromium/Test",
			Interfaces: []introspect.Interface{itf},
		},
	}

	out := new(bytes.Buffer)
	if err := adaptor.Generate(introspections, out, "/tmp/adaptor2.h"); err != nil {
		t.Errorf("Generate got error, want nil: %v", err)
	}

	if diff := cmp.Diff(out.String(), newFileDescriptorsOutput); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}
