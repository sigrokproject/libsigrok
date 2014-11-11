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
functionality in libsigrok. It is built on top of the sigrok++ C++ API.

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
#include <pygobject.h>

PyObject *GLib;
PyTypeObject *IOChannel;
PyTypeObject *PollFD;

#include "config.h"

#if PYGOBJECT_FLAGS_SIGNED
typedef gint pyg_flags_type;
#else
typedef guint pyg_flags_type;
#endif

%}

%init %{
    pygobject_init(-1, -1, -1);
    GLib = PyImport_ImportModule("gi.repository.GLib");
    IOChannel = (PyTypeObject *) PyObject_GetAttrString(GLib, "IOChannel");
    PollFD = (PyTypeObject *) PyObject_GetAttrString(GLib, "PollFD");
%}

/* Map file objects to file descriptors. */
%typecheck(SWIG_TYPECHECK_POINTER) int fd {
    $1 = (PyObject_AsFileDescriptor($input) != -1);
}

%typemap(in) int fd {
    int fd = PyObject_AsFileDescriptor($input);
    if (fd == -1)
        SWIG_exception(SWIG_TypeError,
            "Expected file object or integer file descriptor");
    else
      $1 = fd;
}

/* Map from Glib::Variant to native Python types. */
%typemap(out) Glib::VariantBase {
    GValue *value = g_new0(GValue, 1);
    g_value_init(value, G_TYPE_VARIANT);
    g_value_set_variant(value, $1.gobj());
    PyObject *variant = pyg_value_as_pyobject(value, true);
    $result = PyObject_CallMethod(variant,
        const_cast<char *>("unpack"),
        const_cast<char *>(""), NULL);
    Py_XDECREF(variant);
    g_free(value);
}

/* Map from Glib::IOCondition to GLib.IOCondition. */
%typecheck(SWIG_TYPECHECK_POINTER) Glib::IOCondition {
    pyg_flags_type flags;
    $1 = pygobject_check($input, &PyGFlags_Type) &&
         (pyg_flags_get_value(G_TYPE_IO_CONDITION, $input, &flags) != -1);
}

%typemap(in) Glib::IOCondition {
    if (!pygobject_check($input, &PyGFlags_Type))
        SWIG_exception(SWIG_TypeError, "Expected GLib.IOCondition value");
    pyg_flags_type flags;
    if (pyg_flags_get_value(G_TYPE_IO_CONDITION, $input, &flags) == -1)
        SWIG_exception(SWIG_TypeError, "Not a valid Glib.IOCondition value");
    $1 = (Glib::IOCondition) flags;
}

/* And back */
%typemap(out) Glib::IOCondition {
    GValue *value = g_new0(GValue, 1);
    g_value_init(value, G_TYPE_IO_CONDITION);
    g_value_set_flags(value, &$1);
    $result = pyg_value_as_pyobject(value, true);
    g_free(value);
}

/* Map from GLib.PollFD to Glib::PollFD *. */
%typecheck(SWIG_TYPECHECK_POINTER) Glib::PollFD {
    $1 = pygobject_check($input, PollFD);
}

%typemap(in) Glib::PollFD {
    if (!pygobject_check($input, PollFD))
        SWIG_exception(SWIG_TypeError, "Expected GLib.PollFD");
    PyObject *fd_obj = PyObject_GetAttrString($input, "fd");
    PyObject *events_obj = PyObject_GetAttrString($input, "events");
    pyg_flags_type flags;
    pyg_flags_get_value(G_TYPE_IO_CONDITION, events_obj, &flags);
    int fd = PyInt_AsLong(fd_obj);
    Glib::IOCondition events = (Glib::IOCondition) flags;
    $1 = Glib::PollFD(fd, events);
}

/* Map from GLib.IOChannel to Glib::IOChannel *. */
%typecheck(SWIG_TYPECHECK_POINTER) Glib::RefPtr<Glib::IOChannel> {
    $1 = pygobject_check($input, IOChannel);
}

%typemap(in) Glib::RefPtr<Glib::IOChannel> {
    if (!pygobject_check($input, IOChannel))
        SWIG_exception(SWIG_TypeError, "Expected GLib.IOChannel");
    $1 = Glib::wrap((GIOChannel *) PyObject_Hash($input), true);
}

/* Map from callable PyObject to SourceCallbackFunction. */
%typecheck(SWIG_TYPECHECK_POINTER) sigrok::SourceCallbackFunction {
    $1 = PyCallable_Check($input);
}

%typemap(in) sigrok::SourceCallbackFunction {
    if (!PyCallable_Check($input))
        SWIG_exception(SWIG_TypeError, "Expected a callable Python object");

    $1 = [=] (Glib::IOCondition revents) {
        auto gstate = PyGILState_Ensure();

        GValue *value = g_new0(GValue, 1);
        g_value_init(value, G_TYPE_IO_CONDITION);
        g_value_set_flags(value, revents);
        auto revents_obj = pyg_value_as_pyobject(value, true);
        g_free(value);

        auto arglist = Py_BuildValue("(O)", revents_obj);

        auto result = PyEval_CallObject($input, arglist);

        Py_XDECREF(arglist);
        Py_XDECREF(revents_obj);

        bool completed = !PyErr_Occurred();

        if (!completed)
            PyErr_Print();

        bool valid_result = (completed && PyBool_Check(result));

        if (completed && !valid_result)
        {
            PyErr_SetString(PyExc_TypeError,
                "EventSource callback did not return a boolean");
            PyErr_Print();
        }

        bool retval = (valid_result && result == Py_True);

        Py_XDECREF(result);

        PyGILState_Release(gstate);

        if (!valid_result)
            throw sigrok::Error(SR_ERR);

        return retval;
    };

    Py_XINCREF($input);
}

/* Map from callable PyObject to LogCallbackFunction */
%typecheck(SWIG_TYPECHECK_POINTER) sigrok::LogCallbackFunction {
    $1 = PyCallable_Check($input);
}

%typemap(in) sigrok::LogCallbackFunction {
    if (!PyCallable_Check($input))
        SWIG_exception(SWIG_TypeError, "Expected a callable Python object");

    $1 = [=] (const sigrok::LogLevel *loglevel, string message) {
        auto gstate = PyGILState_Ensure();

        auto log_obj = SWIG_NewPointerObj(
                SWIG_as_voidptr(loglevel), SWIGTYPE_p_sigrok__LogLevel, 0);

        auto string_obj = PyString_FromString(message.c_str());

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

%{

#include "libsigrok/libsigrok.hpp"

/* Convert from a Python dict to a std::map<std::string, std::string> */
std::map<std::string, std::string> dict_to_map_string(PyObject *dict)
{
    if (!PyDict_Check(dict))
        throw sigrok::Error(SR_ERR_ARG);

    std::map<std::string, std::string> output;

    PyObject *py_key, *py_value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(dict, &pos, &py_key, &py_value)) {
        if (!PyString_Check(py_key))
            throw sigrok::Error(SR_ERR_ARG);
        if (!PyString_Check(py_value))
            throw sigrok::Error(SR_ERR_ARG);
        auto key = PyString_AsString(py_key);
        auto value = PyString_AsString(py_value);
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
    else if (type == SR_T_STRING && PyString_Check(input))
        return Glib::Variant<Glib::ustring>::create(PyString_AsString(input));
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
    else if (type == G_VARIANT_TYPE_STRING && PyString_Check(input))
        return Glib::Variant<Glib::ustring>::create(PyString_AsString(input));
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
        if (!PyString_Check(py_key))
            throw sigrok::Error(SR_ERR_ARG);
        auto key = PyString_AsString(py_key);
        auto value = python_to_variant_by_option(py_value, options[key]);
        output[key] = value;
    }

    return output;
}

%}

/* Ignore these methods, we will override them below. */
%ignore sigrok::Driver::scan;
%ignore sigrok::InputFormat::create_input;
%ignore sigrok::OutputFormat::create_output;

%include "doc.i"

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
            if (!PyString_Check(py_key))
                throw sigrok::Error(SR_ERR_ARG);
            auto key = sigrok::ConfigKey::get_by_identifier(PyString_AsString(py_key));
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
