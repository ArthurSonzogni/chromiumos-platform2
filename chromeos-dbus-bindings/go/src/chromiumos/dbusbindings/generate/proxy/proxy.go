// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package proxy outputs client-side bindings classes based on introspects.
package proxy

import (
	"io"
	"strings"
	"text/template"

	"chromiumos/dbusbindings/dbustype"
	"chromiumos/dbusbindings/generate/genutil"
	"chromiumos/dbusbindings/introspect"
	"chromiumos/dbusbindings/serviceconfig"
)

var funcMap = template.FuncMap{
	"extractNameSpaces":      genutil.ExtractNameSpaces,
	"formatComment":          genutil.FormatComment,
	"makeFullItfName":        genutil.MakeFullItfName,
	"makeFullProxyName":      genutil.MakeFullProxyName,
	"makeMethodParams":       makeMethodParams,
	"makeMethodCallbackType": makeMethodCallbackType,
	"makeProxyName":          genutil.MakeProxyName,
	"makePropertyBaseTypeExtract": func(p *introspect.Property) (string, error) {
		return p.BaseType(dbustype.DirectionExtract)
	},
	"makeProxyInArgTypeProxy": func(p *introspect.Property) (string, error) {
		return p.InArgType(dbustype.ReceiverProxy)
	},
	"makeSignalCallbackType": makeSignalCallbackType,
	"makeVariableName":       genutil.MakeVariableName,
	"nindent":                genutil.Nindent,
	"repeat":                 strings.Repeat,
	"reverse":                genutil.Reverse,
}

const (
	templateText = `// Automatic generation of D-Bus interfaces:
{{range .Introspects}}{{range .Interfaces -}}
//  - {{.Name}}
{{end}}{{end -}}

#ifndef {{.HeaderGuard}}
#define {{.HeaderGuard}}
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

{{if .ObjectManagerName -}}
{{range extractNameSpaces .ObjectManagerName -}}
namespace {{.}} {
{{end -}}
class {{makeProxyName .ObjectManagerName}};
{{range extractNameSpaces .ObjectManagerName | reverse -}}
}  // namespace {{.}}
{{end}}
{{end -}}
{{range $introspect := .Introspects}}{{range $itf := .Interfaces -}}

{{range extractNameSpaces .Name -}}
namespace {{.}} {
{{end}}
// Abstract interface proxy for {{makeFullItfName .Name}}.
{{formatComment .DocString 0 -}}
{{- $itfName := makeProxyName .Name | printf "%sInterface" -}}
class {{$itfName}} {
 public:
  virtual ~{{$itfName}}() = default;
{{- range .Methods}}
{{- $inParams := makeMethodParams 0 .InputArguments -}}
{{- $outParams := makeMethodParams (len .InputArguments) .OutputArguments}}

{{formatComment .DocString 2 -}}
{{"  "}}virtual bool {{.Name}}(
{{- range $inParams }}
      {{.Type}} {{.Name}},
{{- end}}
{{- range $outParams }}
      {{.Type}} {{.Name}},
{{- end}}
      brillo::ErrorPtr* error,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;

{{formatComment .DocString 2 -}}
{{"  "}}virtual void {{.Name}}Async(
{{- range $inParams}}
      {{.Type}} {{.Name}},
{{- end}}
      {{makeMethodCallbackType .OutputArguments}} success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;
{{- end}}
{{- range .Signals}}

  virtual void Register{{.Name}}SignalHandler(
      {{- makeSignalCallbackType .Args | nindent 6}} signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) = 0;
{{- end}}
{{- if .Properties}}{{"\n"}}{{end}}
{{- range .Properties}}
{{- $name := makeVariableName .Name -}}
{{- $type := makeProxyInArgTypeProxy . }}
  static const char* {{.Name}}Name() { return "{{.Name}}"; }
  virtual {{$type}} {{$name}}() const = 0;
{{- if eq .Access "readwrite"}}
  virtual void set_{{$name}}({{$type}} value,
                   {{repeat " " (len $name)}} base::OnceCallback<void(bool)> callback) = 0;
{{- end}}
{{- end}}

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
{{- if .Properties}}
{{if $.ObjectManagerName}}
  virtual void SetPropertyChangedCallback(
      const base::RepeatingCallback<void({{$itfName}}*, const std::string&)>& callback) = 0;
{{- else}}
  virtual void InitializeProperties(
      const base::RepeatingCallback<void({{$itfName}}*, const std::string&)>& callback) = 0;
{{- end}}
{{- end}}
};

{{range extractNameSpaces .Name | reverse -}}
}  // namespace {{.}}
{{end}}
{{range extractNameSpaces .Name -}}
namespace {{.}} {
{{end}}
// Interface proxy for {{makeFullItfName .Name}}.
{{formatComment .DocString 0 -}}
{{- $proxyName := makeProxyName .Name -}}
class {{$proxyName}} final : public {{$itfName}} {
 public:
{{- if .Properties }}
  class PropertySet : public dbus::PropertySet {
   public:
    PropertySet(dbus::ObjectProxy* object_proxy,
                const PropertyChangedCallback& callback)
        : dbus::PropertySet{object_proxy,
                            "{{.Name}}",
                            callback} {
{{- range .Properties}}
      RegisterProperty({{.Name}}Name(), &{{makeVariableName .Name}});
{{- end}}
    }
    PropertySet(const PropertySet&) = delete;
    PropertySet& operator=(const PropertySet&) = delete;
{{range .Properties}}
    brillo::dbus_utils::Property<{{makePropertyBaseTypeExtract .}}> {{makeVariableName .Name}};
{{- end}}

  };
{{- end}}
{{- /* TODO(crbug.com/983008): Add constructor */}}
  {{$proxyName}}(const {{$proxyName}}&) = delete;
  {{$proxyName}}& operator=(const {{$proxyName}}&) = delete;

  ~{{$proxyName}}() override {
  }
{{- range .Signals}}

  void Register{{.Name}}SignalHandler(
      {{- makeSignalCallbackType .Args | nindent 6}} signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override {
    brillo::dbus_utils::ConnectToSignal(
        dbus_object_proxy_,
        "{{$itf.Name}}",
        "{{.Name}}",
        signal_callback,
        std::move(on_connected_callback));
  }
{{- end}}

  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

{{- if .Properties}}
{{if $.ObjectManagerName}}
  void SetPropertyChangedCallback(
      const base::RepeatingCallback<void({{$itfName}}*, const std::string&)>& callback) override {
    on_property_changed_ = callback;
  }
{{- else}}
  void InitializeProperties(
      const base::RepeatingCallback<void({{$itfName}}*, const std::string&)>& callback) override {
{{- /* TODO(crbug.com/983008): Use std::make_unique. */}}
    property_set_.reset(
        new PropertySet(dbus_object_proxy_, base::BindRepeating(callback, this)));
    property_set_->ConnectSignals();
    property_set_->GetAll();
  }
{{- end}}

  const PropertySet* GetProperties() const { return &(*property_set_); }
  PropertySet* GetProperties() { return &(*property_set_); }
{{- end}}

{{- range .Methods}}
{{- $inParams := makeMethodParams 0 .InputArguments -}}
{{- $outParams := makeMethodParams (len .InputArguments) .OutputArguments}}

{{formatComment .DocString 2 -}}
{{"  "}}bool {{.Name}}(
{{- range $inParams }}
      {{.Type}} {{.Name}},
{{- end}}
{{- range $outParams }}
      {{.Type}} {{.Name}},
{{- end}}
      brillo::ErrorPtr* error,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) override {
    auto response = brillo::dbus_utils::CallMethodAndBlockWithTimeout(
        timeout_ms,
        dbus_object_proxy_,
        "{{$itf.Name}}",
        "{{.Name}}",
        error
{{- range $inParams }},
        {{.Name}}
{{- end}});
    return response && brillo::dbus_utils::ExtractMethodCallResults(
        response.get(), error{{range $i, $param := $outParams}}, {{.Name}}{{end}});
  }

{{formatComment .DocString 2 -}}
{{"  "}}void {{.Name}}Async(
{{- range $inParams}}
      {{.Type}} {{.Name}},
{{- end}}
      {{makeMethodCallbackType .OutputArguments}} success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) override {
    brillo::dbus_utils::CallMethodWithTimeout(
        timeout_ms,
        dbus_object_proxy_,
        "{{$itf.Name}}",
        "{{.Name}}",
        std::move(success_callback),
        std::move(error_callback)
{{- range $inParams}},
        {{.Name}}
{{- end}});
  }

{{- end}}

{{- range .Properties}}
{{- $name := makeVariableName .Name -}}
{{- $type := makeProxyInArgTypeProxy . }}

  {{$type}} {{$name}}() const override {
    return property_set_->{{$name}}.value();
  }
{{- if eq .Access "readwrite"}}

  void set_{{$name}}({{$type}} value,
           {{repeat " " (len $name)}} base::OnceCallback<void(bool)> callback) override {
    property_set_->{{$name}}.Set(value, std::move(callback));
  }
{{- end}}
{{- end}}

 private:
{{- if and $.ObjectManagerName .Properties}}
  void OnPropertyChanged(const std::string& property_name) {
    if (!on_property_changed_.is_null())
      on_property_changed_.Run(this, property_name);
  }
{{/* blank line separator */}}
{{- end}}
  scoped_refptr<dbus::Bus> bus_;
{{- if $.ServiceName}}
  const std::string service_name_{"{{$.ServiceName}}"};
{{- else}}
  std::string service_name_;
{{- end}}

{{- if $introspect.Name}}
  const dbus::ObjectPath object_path_{"{{$introspect.Name}}"};
{{- else}}
  dbus::ObjectPath object_path_;
{{- end}}
{{- if and $.ObjectManagerName .Properties}}
  PropertySet* property_set_;
  base::RepeatingCallback<void({{$itfName}}*, const std::string&)> on_property_changed_;
{{- end}}
  dbus::ObjectProxy* dbus_object_proxy_;
{{- if and (not $.ObjectManagerName) .Properties}}
  std::unique_ptr<PropertySet> property_set_;
{{- end}}
{{- if and $.ObjectManagerName .Properties}}
  friend class {{makeFullProxyName $.ObjectManagerName}};
{{- end}}

};

{{range extractNameSpaces .Name | reverse -}}
}  // namespace {{.}}
{{end}}
{{end}}{{end -}}
{{- /* TODO(crbug.com/983008): Convert ObjectManager::GenerateProxy */ -}}
#endif  // {{.HeaderGuard}}
`
)

// Generate outputs the header file containing proxy interfaces into f.
// outputFilePath is used to make a unique header guard.
func Generate(introspects []introspect.Introspection, f io.Writer, outputFilePath string, config serviceconfig.Config) error {
	tmpl, err := template.New("proxy").Funcs(funcMap).Parse(templateText)
	if err != nil {
		return err
	}

	headerGuard := genutil.GenerateHeaderGuard(outputFilePath)
	return tmpl.Execute(f, struct {
		Introspects       []introspect.Introspection
		HeaderGuard       string
		ServiceName       string
		ObjectManagerName string
	}{
		Introspects:       introspects,
		HeaderGuard:       headerGuard,
		ServiceName:       config.ServiceName,
		ObjectManagerName: config.ObjectManager.Name,
	})
}
