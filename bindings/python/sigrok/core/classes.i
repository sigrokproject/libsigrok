/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Martin Ling <martin-sigrok@earth.li>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

%define DOCSTRING
"@mainpage API Reference

Introduction
------------

The pysigrok API provides an object-oriented Python interface to the
functionality in libsigrok. It is built on top of the libsigrokcxx C++ API.

Getting started
---------------

Usage of the pysigrok API needs to begin with a call to Context.create().
This will create the global libsigrok context and returns a Context object.
Methods on this object provide access to the hardware drivers, input and output
formats supported by the library, as well as means of creating other objects
such as sessions and triggers.

Error handling
--------------

When any libsigrok C API call returns an error, an Error exception is raised,
which provides access to the error code and description."
%enddef

%module(docstring=DOCSTRING) classes

%{
#include "config.h"

#include <stdio.h>
#include <pygobject.h>
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>

PyObject *PyGObject_lib;
PyObject *GLib;

#if PYGOBJECT_FLAGS_SIGNED
typedef gint pyg_flags_type;
#else
typedef guint pyg_flags_type;
#endif

#if PY_VERSION_HEX >= 0x03000000
#define string_check PyUnicode_Check
#define string_from_python PyUnicode_AsUTF8
#define string_to_python PyUnicode_FromString
#else
#define string_check PyString_Check
#define string_from_python PyString_AsString
#define string_to_python PyString_FromString
#endif

%}

%init %{
    PyGObject_lib = pygobject_init(-1, -1, -1);
    if (!PyGObject_lib)
        fprintf(stderr, "pygobject initialization failed.\n");
    GLib = PyImport_ImportModule("gi.repository.GLib");
    /*
     * This check can't save us if the import fails, but at least it gives us
     * a starting point to trace the issue versus straight out crashing.
     */
    if (!GLib) {
        fprintf(stderr, "Import of gi.repository.GLib failed.\n");
#if PY_VERSION_HEX >= 0x03000000
        return nullptr;
#else
        return;
#endif
    }
    import_array();
%}

%include "../../../swig/templates.i"

/* Map file objects to file descriptors. */
%typecheck(SWIG_TYPECHECK_POINTER) int fd {
    $1 = (PyObject_AsFileDescriptor($input) != -1);
}

/* Map from Glib::Variant to native Python types. */
%typemap(out) Glib::VariantBase {
    GValue *value = g_new0(GValue, 1);
    g_value_init(value, G_TYPE_VARIANT);
    g_value_set_variant(value, $1.gobj());
    PyObject *variant = pyg_value_as_pyobject(value, true);
    $result = PyObject_CallMethod(variant,
        const_cast<char *>("unpack"), nullptr);
    Py_XDECREF(variant);
    g_free(value);
}

/* Map from callable PyObject to LogCallbackFunction */
%typecheck(SWIG_TYPECHECK_POINTER) sigrok::LogCallbackFunction {
    $1 = PyCallable_Check($input);
}

%typemap(in) sigrok::LogCallbackFunction {
    if (!PyCallable_Check($input))
        SWIG_exception(SWIG_TypeError, "Expected a callable Python object");

    $1 = [=] (const sigrok::LogLevel *loglevel, std::string message) {
        auto gstate = PyGILState_Ensure();

        auto log_obj = SWIG_NewPointerObj(
                SWIG_as_voidptr(loglevel), SWIGTYPE_p_sigrok__LogLevel, 0);

        auto string_obj = string_to_python(message.c_str());

        auto arglist = Py_BuildValue("(OO)", log_obj, string_obj);

        auto result = PyEval_CallObject($input, arglist);

        Py_XDECREF(arglist);
        Py_XDECREF(log_obj);
        Py_XDECREF(string_obj);

        bool completed = !PyErr_Occurred();

        if (!completed)
            PyErr_Print();

        bool valid_result = (completed && result == Py_None);

        Py_XDECREF(result);

        if (completed && !valid_result)
        {
            PyErr_SetString(PyExc_TypeError,
                "Log callback did not return None");
            PyErr_Print();
        }

        PyGILState_Release(gstate);

        if (!valid_result)
            throw sigrok::Error(SR_ERR);
    };

    Py_XINCREF($input);
}

/* Map from callable PyObject to SessionStoppedCallback */
%typecheck(SWIG_TYPECHECK_POINTER) sigrok::SessionStoppedCallback {
    $1 = PyCallable_Check($input);
}

%typemap(in) sigrok::SessionStoppedCallback {
    if (!PyCallable_Check($input))
        SWIG_exception(SWIG_TypeError, "Expected a callable Python object");

    $1 = [=] () {
        const auto gstate = PyGILState_Ensure();

        const auto result = PyEval_CallObject($input, nullptr);
        const bool completed = !PyErr_Occurred();
        const bool valid_result = (completed && result == Py_None);

        if (completed && !valid_result) {
            PyErr_SetString(PyExc_TypeError,
                "Session stop callback did not return None");
        }
        if (!valid_result)
            PyErr_Print();

        Py_XDECREF(result);
        PyGILState_Release(gstate);

        if (!valid_result)
            throw sigrok::Error(SR_ERR);
    };

    Py_XINCREF($input);
}

/* Map from callable PyObject to DatafeedCallbackFunction */
%typecheck(SWIG_TYPECHECK_POINTER) sigrok::DatafeedCallbackFunction {
    $1 = PyCallable_Check($input);
}

%typemap(in) sigrok::DatafeedCallbackFunction {
    if (!PyCallable_Check($input))
        SWIG_exception(SWIG_TypeError, "Expected a callable Python object");

    $1 = [=] (std::shared_ptr<sigrok::Device> device,
            std::shared_ptr<sigrok::Packet> packet) {
        auto gstate = PyGILState_Ensure();

        auto device_obj = SWIG_NewPointerObj(
            SWIG_as_voidptr(new std::shared_ptr<sigrok::Device>(device)),
            SWIGTYPE_p_std__shared_ptrT_sigrok__Device_t, SWIG_POINTER_OWN);

        auto packet_obj = SWIG_NewPointerObj(
            SWIG_as_voidptr(new std::shared_ptr<sigrok::Packet>(packet)),
            SWIGTYPE_p_std__shared_ptrT_sigrok__Packet_t, SWIG_POINTER_OWN);

        auto arglist = Py_BuildValue("(OO)", device_obj, packet_obj);

        auto result = PyEval_CallObject($input, arglist);

        Py_XDECREF(arglist);
        Py_XDECREF(device_obj);
        Py_XDECREF(packet_obj);

        bool completed = !PyErr_Occurred();

        if (!completed)
            PyErr_Print();

        bool valid_result = (completed && result == Py_None);

        Py_XDECREF(result);

        if (completed && !valid_result)
        {
            PyErr_SetString(PyExc_TypeError,
                "Datafeed callback did not return None");
            PyErr_Print();
        }

        PyGILState_Release(gstate);

        if (!valid_result)
            throw sigrok::Error(SR_ERR);
    };

    Py_XINCREF($input);
}

/* Cast PacketPayload pointers to correct subclass type. */
%ignore sigrok::Packet::payload;

%extend sigrok::Packet
{
    std::shared_ptr<sigrok::Header> _payload_header()
    {
        return dynamic_pointer_cast<sigrok::Header>($self->payload());
    }
    std::shared_ptr<sigrok::Meta> _payload_meta()
    {
        return dynamic_pointer_cast<sigrok::Meta>($self->payload());
    }
    std::shared_ptr<sigrok::Analog> _payload_analog()
    {
        return dynamic_pointer_cast<sigrok::Analog>($self->payload());
    }
    std::shared_ptr<sigrok::Logic> _payload_logic()
    {
        return dynamic_pointer_cast<sigrok::Logic>($self->payload());
    }
}

%extend sigrok::Packet
{
%pythoncode
{
    def _payload(self):
        if self.type == PacketType.HEADER:
            return self._payload_header()
        elif self.type == PacketType.META:
            return self._payload_meta()
        elif self.type == PacketType.LOGIC:
            return self._payload_logic()
        elif self.type == PacketType.ANALOG:
            return self._payload_analog()
        else:
            return None

    payload = property(_payload)
}
}

%{

#include "libsigrokcxx/libsigrokcxx.hpp"

/* Convert from a Python dict to a std::map<std::string, std::string> */
std::map<std::string, std::string> dict_to_map_string(PyObject *dict)
{
    if (!PyDict_Check(dict))
        throw sigrok::Error(SR_ERR_ARG);

    std::map<std::string, std::string> output;

    PyObject *py_key, *py_value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(dict, &pos, &py_key, &py_value)) {
        if (!string_check(py_key))
            throw sigrok::Error(SR_ERR_ARG);
        if (!string_check(py_value))
            throw sigrok::Error(SR_ERR_ARG);
        auto key = string_from_python(py_key);
        auto value = string_from_python(py_value);
        output[key] = value;
    }

    return output;
}

/* Convert from a Python type to Glib::Variant, according to config key data type. */
Glib::VariantBase python_to_variant_by_key(PyObject *input, const sigrok::ConfigKey *key)
{
    enum sr_datatype type = (enum sr_datatype) key->data_type()->id();

    if (type == SR_T_UINT64 && PyInt_Check(input))
        return Glib::Variant<guint64>::create(PyInt_AsLong(input));
    if (type == SR_T_UINT64 && PyLong_Check(input))
        return Glib::Variant<guint64>::create(PyLong_AsLong(input));
    else if (type == SR_T_STRING && string_check(input))
        return Glib::Variant<Glib::ustring>::create(string_from_python(input));
    else if (type == SR_T_BOOL && PyBool_Check(input))
        return Glib::Variant<bool>::create(input == Py_True);
    else if (type == SR_T_FLOAT && PyFloat_Check(input))
        return Glib::Variant<double>::create(PyFloat_AsDouble(input));
    else if (type == SR_T_INT32 && PyInt_Check(input))
        return Glib::Variant<gint32>::create(PyInt_AsLong(input));
    else
        throw sigrok::Error(SR_ERR_ARG);
}

/* Convert from a Python type to Glib::Variant, according to Option data type. */
Glib::VariantBase python_to_variant_by_option(PyObject *input,
    std::shared_ptr<sigrok::Option> option)
{
    GVariantType *type = option->default_value().get_type().gobj();

    if (type == G_VARIANT_TYPE_UINT64 && PyInt_Check(input))
        return Glib::Variant<guint64>::create(PyInt_AsLong(input));
    if (type == G_VARIANT_TYPE_UINT64 && PyLong_Check(input))
        return Glib::Variant<guint64>::create(PyLong_AsLong(input));
    else if (type == G_VARIANT_TYPE_STRING && string_check(input))
        return Glib::Variant<Glib::ustring>::create(string_from_python(input));
    else if (type == G_VARIANT_TYPE_BOOLEAN && PyBool_Check(input))
        return Glib::Variant<bool>::create(input == Py_True);
    else if (type == G_VARIANT_TYPE_DOUBLE && PyFloat_Check(input))
        return Glib::Variant<double>::create(PyFloat_AsDouble(input));
    else if (type == G_VARIANT_TYPE_INT32 && PyInt_Check(input))
        return Glib::Variant<gint32>::create(PyInt_AsLong(input));
    else
        throw sigrok::Error(SR_ERR_ARG);
}

/* Convert from a Python dict to a std::map<std::string, std::string> */
std::map<std::string, Glib::VariantBase> dict_to_map_options(PyObject *dict,
    std::map<std::string, std::shared_ptr<sigrok::Option> > options)
{
    if (!PyDict_Check(dict))
        throw sigrok::Error(SR_ERR_ARG);

    std::map<std::string, Glib::VariantBase> output;

    PyObject *py_key, *py_value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(dict, &pos, &py_key, &py_value)) {
        if (!string_check(py_key))
            throw sigrok::Error(SR_ERR_ARG);
        auto key = string_from_python(py_key);
        auto value = python_to_variant_by_option(py_value, options[key]);
        output[key] = value;
    }

    return output;
}

%}

/* Ignore these methods, we will override them below. */
%ignore sigrok::Analog::data;
%ignore sigrok::Logic::data;
%ignore sigrok::Driver::scan;
%ignore sigrok::InputFormat::create_input;
%ignore sigrok::OutputFormat::create_output;

%include "doc_start.i"

%define %attributevector(Class, Type, Name, Get)
%rename(_ ## Get) sigrok::Class::Get;
%extend sigrok::Class
{
%pythoncode
{
  Name = property(_ ## Get)
}
}
%enddef

%define %attributemap(Class, Type, Name, Get)
%rename(_ ## Get) sigrok::Class::Get;
%extend sigrok::Class
{
%pythoncode
{
  Name = property(fget = lambda x: x._ ## Get().asdict(), doc=_ ## Get.__doc__)
}
}
%enddef

%define %enumextras(Class)
%extend sigrok::Class
{
  long __hash__()
  {
    return (long) $self;
  }

  std::string __str__()
  {
    return $self->name();
  }

  std::string __repr__()
  {
    return "Class." + $self->name();
  }

%pythoncode
{
  def __eq__(self, other):
    return (type(self) is type(other) and hash(self) == hash(other))

  def __ne__(self, other):
    return (type(self) is not type(other) or hash(self) != hash(other))
}
}
%enddef

%include "../../../swig/classes.i"

/* Support Driver.scan() with keyword arguments. */
%extend sigrok::Driver
{
    std::vector<std::shared_ptr<sigrok::HardwareDevice> > _scan_kwargs(PyObject *dict)
    {
        if (!PyDict_Check(dict))
            throw sigrok::Error(SR_ERR_ARG);

        PyObject *py_key, *py_value;
        Py_ssize_t pos = 0;
        std::map<const sigrok::ConfigKey *, Glib::VariantBase> options;

        while (PyDict_Next(dict, &pos, &py_key, &py_value))
        {
            if (!string_check(py_key))
                throw sigrok::Error(SR_ERR_ARG);
            auto key = sigrok::ConfigKey::get_by_identifier(string_from_python(py_key));
            auto value = python_to_variant_by_key(py_value, key);
            options[key] = value;
        }

        return $self->scan(options);
    }
}

%pythoncode
{
    def _Driver_scan(self, **kwargs):
        return self._scan_kwargs(kwargs)

    Driver.scan = _Driver_scan
}

/* Support InputFormat.create_input() with keyword arguments. */
%extend sigrok::InputFormat
{
    std::shared_ptr<sigrok::Input> _create_input_kwargs(PyObject *dict)
    {
        return $self->create_input(
            dict_to_map_options(dict, $self->options()));
    }
}

%pythoncode
{
    def _InputFormat_create_input(self, **kwargs):
        return self._create_input(kwargs)

    InputFormat.create_input = _InputFormat_create_input
}

/* Support OutputFormat.create_output() with keyword arguments. */
%extend sigrok::OutputFormat
{
    std::shared_ptr<sigrok::Output> _create_output_kwargs(
        std::shared_ptr<sigrok::Device> device, PyObject *dict)
    {
        return $self->create_output(device,
            dict_to_map_options(dict, $self->options()));
    }
}

%pythoncode
{
    def _OutputFormat_create_output(self, device, **kwargs):
        return self._create_output_kwargs(device, kwargs)

    OutputFormat.create_output = _OutputFormat_create_output
}

/* Support config_set() with Python input types. */
%extend sigrok::Configurable
{
    void config_set(const ConfigKey *key, PyObject *input)
    {
        $self->config_set(key, python_to_variant_by_key(input, key));
    }
}

/* Return NumPy array from Analog::data(). */
%extend sigrok::Analog
{
    PyObject * _data()
    {
        int nd = 2;
        npy_intp dims[2];
        dims[0] = $self->channels().size();
        dims[1] = $self->num_samples();
        int typenum = NPY_FLOAT;
        void *data = $self->data_pointer();
        return PyArray_SimpleNewFromData(nd, dims, typenum, data);
    }

%pythoncode
{
    data = property(_data)
}
}

/* Return NumPy array from Logic::data(). */
%extend sigrok::Logic
{
    PyObject * _data()
    {
        npy_intp dims[2];
        dims[0] = $self->data_length() / $self->unit_size();
        dims[1] = $self->unit_size();
        int typenum = NPY_UINT8;
        void *data = $self->data_pointer();
        return PyArray_SimpleNewFromData(2, dims, typenum, data);
    }

%pythoncode
{
    data = property(_data)
}
}

/* Create logic packet from Python buffer. */
%extend sigrok::Context
{
    std::shared_ptr<Packet> _create_logic_packet_buf(PyObject *buf, unsigned int unit_size)
    {
        Py_buffer view;
        PyObject_GetBuffer(buf, &view, PyBUF_SIMPLE);
        return $self->create_logic_packet(view.buf, view.len, unit_size);
    }
}

%pythoncode
{
    def _Context_create_logic_packet(self, buf, unit_size):
        return self._create_logic_packet_buf(buf, unit_size)

    Context.create_logic_packet = _Context_create_logic_packet
}

%include "doc_end.i"
