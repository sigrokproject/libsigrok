/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Aurelien Jacobs <aurel@gnuage.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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
"
= Introduction

The sigrok Ruby API provides an object-oriented Ruby interface to the
functionality in libsigrok. It is built on top of the libsigrokcxx C++ API.

= Getting started

Usage of the sigrok Ruby API needs to begin with a call to Context.create().
This will create the global libsigrok context and returns a Context object.
Methods on this object provide access to the hardware drivers, input and output
formats supported by the library, as well as means of creating other objects
such as sessions and triggers.

= Error handling

When any libsigrok C API call returns an error, an Error exception is raised,
which provides access to the error code and description."
%enddef

%module(docstring=DOCSTRING) sigrok

%{
#include "config.h"

#include <stdio.h>
#include <glibmm.h>
%}

%include "../swig/templates.i"

%{
static const char *string_from_ruby(VALUE obj)
{
    switch (TYPE(obj)) {
    case T_STRING:
        return StringValueCStr(obj);
    case T_SYMBOL:
        return rb_id2name(SYM2ID(obj));
    default:
        throw sigrok::Error(SR_ERR_ARG);
    }
}

/* Convert from Glib::Variant to native Ruby types. */
static VALUE variant_to_ruby(Glib::VariantBase variant)
{
    if (variant.is_of_type(Glib::VARIANT_TYPE_BOOL)) {
        return Glib::VariantBase::cast_dynamic< Glib::Variant<bool> >(variant).get() ? Qtrue : Qfalse;
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_BYTE)) {
        return UINT2NUM(Glib::VariantBase::cast_dynamic< Glib::Variant<unsigned char> >(variant).get());
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_INT16)) {
        return INT2NUM(Glib::VariantBase::cast_dynamic< Glib::Variant<gint16> >(variant).get());
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_UINT16)) {
        return UINT2NUM(Glib::VariantBase::cast_dynamic< Glib::Variant<guint16> >(variant).get());
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_INT32)) {
        return INT2NUM(Glib::VariantBase::cast_dynamic< Glib::Variant<gint32> >(variant).get());
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_UINT32)) {
        return UINT2NUM(Glib::VariantBase::cast_dynamic< Glib::Variant<guint32> >(variant).get());
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_INT64)) {
        return LL2NUM(Glib::VariantBase::cast_dynamic< Glib::Variant<gint64> >(variant).get());
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_UINT64)) {
        return ULL2NUM(Glib::VariantBase::cast_dynamic< Glib::Variant<guint64> >(variant).get());
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_DOUBLE)) {
        return DBL2NUM(Glib::VariantBase::cast_dynamic< Glib::Variant<double> >(variant).get());
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_STRING)) {
        auto str = Glib::VariantBase::cast_dynamic< Glib::Variant<std::string> >(variant).get();
        return rb_str_new(str.c_str(), str.length());
    } else if (variant.is_of_type(Glib::VARIANT_TYPE_VARIANT)) {
        auto var = Glib::VariantBase::cast_dynamic< Glib::Variant<Glib::VariantBase> >(variant).get();
        return variant_to_ruby(var);
    } else if (variant.is_container()) {
        Glib::VariantContainerBase container = Glib::VariantBase::cast_dynamic< Glib::VariantContainerBase > (variant);
        gsize count = container.get_n_children();
        if (container.is_of_type(Glib::VARIANT_TYPE_DICTIONARY)) {
            VALUE hash = rb_hash_new();
            for (gsize i = 0; i < count; i++) {
                Glib::VariantContainerBase entry = Glib::VariantBase::cast_dynamic< Glib::VariantContainerBase > (container.get_child(i));
                VALUE key = variant_to_ruby(entry.get_child(0));
                VALUE val = variant_to_ruby(entry.get_child(1));
                rb_hash_aset(hash, key, val);
            }
            return hash;
        } else if (container.is_of_type(Glib::VARIANT_TYPE_ARRAY) ||
                   container.is_of_type(Glib::VARIANT_TYPE_TUPLE)) {
            VALUE array = rb_ary_new2(count);
            for (gsize i = 0; i < count; i++) {
                VALUE val = variant_to_ruby(container.get_child(i));
                rb_ary_store(array, i, val);
            }
            return array;
        }
    } else {
        SWIG_exception(SWIG_TypeError, ("TODO: GVariant(" + variant.get_type().get_string() + ") -> Ruby").c_str());
    }
    return 0; /* Dummy, to avoid a compiler warning. */
}
%}

/* Map from Glib::Variant to native Ruby types. */
%typemap(out) Glib::VariantBase {
    $result = variant_to_ruby($1);
}

/* Map from Glib::VariantContainer to native Ruby types. */
%typemap(out) Glib::VariantContainerBase {
    $result = variant_to_ruby($1);
}

/* Map from callable Ruby Proc to LogCallbackFunction */
%typemap(in) sigrok::LogCallbackFunction {
    if (!rb_obj_is_proc($input))
        SWIG_exception(SWIG_TypeError, "Expected a callable Ruby object");

    std::shared_ptr<VALUE> proc(new VALUE($input), rb_gc_unregister_address);
    rb_gc_register_address(proc.get());

    $1 = [=] (const sigrok::LogLevel *loglevel, std::string message) {
        VALUE log_obj = SWIG_NewPointerObj(
                SWIG_as_voidptr(loglevel), SWIGTYPE_p_sigrok__LogLevel, 0);

        VALUE string_obj = rb_external_str_new_with_enc(message.c_str(), message.length(), rb_utf8_encoding());

        VALUE args = rb_ary_new3(2, log_obj, string_obj);
        rb_proc_call(*proc.get(), args);
    };
}

/* Map from callable Ruby Proc to SessionStoppedCallback */
%typemap(in) sigrok::SessionStoppedCallback {
    if (!rb_obj_is_proc($input))
        SWIG_exception(SWIG_TypeError, "Expected a callable Ruby object");

    std::shared_ptr<VALUE> proc(new VALUE($input), rb_gc_unregister_address);
    rb_gc_register_address(proc.get());

    $1 = [=] () {
        rb_proc_call(*proc.get(), rb_ary_new());
    };
}

/* Map from callable Ruby Proc to DatafeedCallbackFunction */
%typemap(in) sigrok::DatafeedCallbackFunction {
    if (!rb_obj_is_proc($input))
        SWIG_exception(SWIG_TypeError, "Expected a callable Ruby object");

    std::shared_ptr<VALUE> proc(new VALUE($input), rb_gc_unregister_address);
    rb_gc_register_address(proc.get());

    $1 = [=] (std::shared_ptr<sigrok::Device> device,
            std::shared_ptr<sigrok::Packet> packet) {
        VALUE device_obj = SWIG_NewPointerObj(
            SWIG_as_voidptr(new std::shared_ptr<sigrok::Device>(device)),
            SWIGTYPE_p_std__shared_ptrT_sigrok__Device_t, SWIG_POINTER_OWN);

        VALUE packet_obj = SWIG_NewPointerObj(
            SWIG_as_voidptr(new std::shared_ptr<sigrok::Packet>(packet)),
            SWIGTYPE_p_std__shared_ptrT_sigrok__Packet_t, SWIG_POINTER_OWN);

        VALUE args = rb_ary_new3(2, device_obj, packet_obj);
        rb_proc_call(*proc.get(), args);
    };
}

/* Cast PacketPayload pointers to correct subclass type. */
%ignore sigrok::Packet::payload;
%rename sigrok::Packet::_payload payload;
%extend sigrok::Packet
{
    VALUE _payload()
    {
        if ($self->type() == sigrok::PacketType::HEADER) {
            return SWIG_NewPointerObj(
                SWIG_as_voidptr(new std::shared_ptr<sigrok::Header>(dynamic_pointer_cast<sigrok::Header>($self->payload()))),
                SWIGTYPE_p_std__shared_ptrT_sigrok__Header_t, SWIG_POINTER_OWN);
        } else if ($self->type() == sigrok::PacketType::META) {
            return SWIG_NewPointerObj(
                SWIG_as_voidptr(new std::shared_ptr<sigrok::Meta>(dynamic_pointer_cast<sigrok::Meta>($self->payload()))),
                SWIGTYPE_p_std__shared_ptrT_sigrok__Meta_t, SWIG_POINTER_OWN);
        } else if ($self->type() == sigrok::PacketType::ANALOG) {
            return SWIG_NewPointerObj(
                SWIG_as_voidptr(new std::shared_ptr<sigrok::Analog>(dynamic_pointer_cast<sigrok::Analog>($self->payload()))),
                SWIGTYPE_p_std__shared_ptrT_sigrok__Analog_t, SWIG_POINTER_OWN);
        } else if ($self->type() == sigrok::PacketType::LOGIC) {
            return SWIG_NewPointerObj(
                SWIG_as_voidptr(new std::shared_ptr<sigrok::Logic>(dynamic_pointer_cast<sigrok::Logic>($self->payload()))),
                SWIGTYPE_p_std__shared_ptrT_sigrok__Logic_t, SWIG_POINTER_OWN);
        } else {
            return Qnil;
        }
    }
}

%{

#include "libsigrokcxx/libsigrokcxx.hpp"

/* Convert from a Ruby type to Glib::Variant, according to config key data type. */
Glib::VariantBase ruby_to_variant_by_key(VALUE input, const sigrok::ConfigKey *key)
{
    enum sr_datatype type = (enum sr_datatype) key->data_type()->id();

    if (type == SR_T_UINT64 && RB_TYPE_P(input, T_FIXNUM))
        return Glib::Variant<guint64>::create(NUM2ULL(input));
    if (type == SR_T_UINT64 && RB_TYPE_P(input, T_BIGNUM))
        return Glib::Variant<guint64>::create(NUM2ULL(input));
    else if (type == SR_T_STRING && RB_TYPE_P(input, T_STRING))
        return Glib::Variant<Glib::ustring>::create(string_from_ruby(input));
    else if (type == SR_T_STRING && RB_TYPE_P(input, T_SYMBOL))
        return Glib::Variant<Glib::ustring>::create(string_from_ruby(input));
    else if (type == SR_T_BOOL && RB_TYPE_P(input, T_TRUE))
        return Glib::Variant<bool>::create(true);
    else if (type == SR_T_BOOL && RB_TYPE_P(input, T_FALSE))
        return Glib::Variant<bool>::create(false);
    else if (type == SR_T_FLOAT && RB_TYPE_P(input, T_FLOAT))
        return Glib::Variant<double>::create(RFLOAT_VALUE(input));
    else if (type == SR_T_INT32 && RB_TYPE_P(input, T_FIXNUM))
        return Glib::Variant<gint32>::create(NUM2INT(input));
    else
        throw sigrok::Error(SR_ERR_ARG);
}

/* Convert from a Ruby type to Glib::Variant, according to Option data type. */
Glib::VariantBase ruby_to_variant_by_option(VALUE input, std::shared_ptr<sigrok::Option> option)
{
    Glib::VariantBase variant = option->default_value();

    if (variant.is_of_type(Glib::VARIANT_TYPE_UINT64) && RB_TYPE_P(input, T_FIXNUM))
        return Glib::Variant<guint64>::create(NUM2ULL(input));
    else if (variant.is_of_type(Glib::VARIANT_TYPE_UINT64) && RB_TYPE_P(input, T_BIGNUM))
        return Glib::Variant<guint64>::create(NUM2ULL(input));
    else if (variant.is_of_type(Glib::VARIANT_TYPE_STRING) && RB_TYPE_P(input, T_STRING))
        return Glib::Variant<Glib::ustring>::create(string_from_ruby(input));
    else if (variant.is_of_type(Glib::VARIANT_TYPE_STRING) && RB_TYPE_P(input, T_SYMBOL))
        return Glib::Variant<Glib::ustring>::create(string_from_ruby(input));
    else if (variant.is_of_type(Glib::VARIANT_TYPE_BOOL) && RB_TYPE_P(input, T_TRUE))
        return Glib::Variant<bool>::create(true);
    else if (variant.is_of_type(Glib::VARIANT_TYPE_BOOL) && RB_TYPE_P(input, T_FALSE))
        return Glib::Variant<bool>::create(false);
    else if (variant.is_of_type(Glib::VARIANT_TYPE_DOUBLE) && RB_TYPE_P(input, T_FLOAT))
        return Glib::Variant<double>::create(RFLOAT_VALUE(input));
    else if (variant.is_of_type(Glib::VARIANT_TYPE_INT32) && RB_TYPE_P(input, T_FIXNUM))
        return Glib::Variant<gint32>::create(NUM2INT(input));
    else
        throw sigrok::Error(SR_ERR_ARG);
}

struct hash_to_map_options_params {
    std::map<std::string, std::shared_ptr<sigrok::Option> > options;
    std::map<std::string, Glib::VariantBase> output;
};

int convert_option(VALUE key, VALUE val, VALUE in) {
    struct hash_to_map_options_params *params;
    params = (struct hash_to_map_options_params *)in;

    auto k = string_from_ruby(key);
    auto v = ruby_to_variant_by_option(val, params->options[k]);
    params->output[k] = v;

    return ST_CONTINUE;
}

/* Convert from a Ruby hash to a std::map<std::string, Glib::VariantBase> */
std::map<std::string, Glib::VariantBase> hash_to_map_options(VALUE hash,
    std::map<std::string, std::shared_ptr<sigrok::Option> > options)
{
    if (!RB_TYPE_P(hash, T_HASH))
        throw sigrok::Error(SR_ERR_ARG);

    struct hash_to_map_options_params params = { options };
    rb_hash_foreach(hash, (int (*)(ANYARGS))convert_option, (VALUE)&params);

    return params.output;
}

int convert_option_by_key(VALUE key, VALUE val, VALUE in) {
    std::map<const sigrok::ConfigKey *, Glib::VariantBase> *options;
    options = (std::map<const sigrok::ConfigKey *, Glib::VariantBase> *)in;

    auto k = sigrok::ConfigKey::get_by_identifier(string_from_ruby(key));
    auto v = ruby_to_variant_by_key(val, k);
    (*options)[k] = v;

    return ST_CONTINUE;
}

%}

/* Ignore these methods, we will override them below. */
%ignore sigrok::Analog::data;
%ignore sigrok::Driver::scan;
%ignore sigrok::Input::send;
%ignore sigrok::InputFormat::create_input;
%ignore sigrok::OutputFormat::create_output;

%include "doc.i"

%define %attributevector(Class, Type, Name, Get)
%alias sigrok::Class::_ ## Get #Name;
%enddef

%define %attributemap(Class, Type, Name, Get)
%alias sigrok::Class::_ ## Get #Name;
%enddef

%define %enumextras(Class)
%extend sigrok::Class
{
    VALUE to_s()
    {
        std::string str = $self->name();
        return rb_external_str_new_with_enc(str.c_str(), str.length(), rb_utf8_encoding());
    }

    bool operator==(void *other)
    {
        return (long) $self == (long) other;
    }
}
%enddef

%include "../swig/classes.i"

/* Replace the original Driver.scan with a keyword arguments version. */
%rename sigrok::Driver::_scan scan;
%extend sigrok::Driver
{
    std::vector<std::shared_ptr<sigrok::HardwareDevice> > _scan(VALUE kwargs = rb_hash_new())
    {
        if (!RB_TYPE_P(kwargs, T_HASH))
            throw sigrok::Error(SR_ERR_ARG);

        std::map<const sigrok::ConfigKey *, Glib::VariantBase> options;
        rb_hash_foreach(kwargs, (int (*)(ANYARGS))convert_option_by_key, (VALUE)&options);

        return $self->scan(options);
    }
}

/* Support Input.send() with string argument. */
%rename sigrok::Input::_send send;
%extend sigrok::Input
{
    void _send(VALUE data)
    {
        data = StringValue(data);
        return $self->send(RSTRING_PTR(data), RSTRING_LEN(data));
    }
}

/* Support InputFormat.create_input() with keyword arguments. */
%rename sigrok::InputFormat::_create_input create_input;
%extend sigrok::InputFormat
{
    std::shared_ptr<sigrok::Input> _create_input(VALUE hash = rb_hash_new())
    {
        return $self->create_input(hash_to_map_options(hash, $self->options()));
    }
}

/* Support OutputFormat.create_output() with keyword arguments. */
%rename sigrok::OutputFormat::_create_output create_output;
%extend sigrok::OutputFormat
{
    std::shared_ptr<sigrok::Output> _create_output(
        std::shared_ptr<sigrok::Device> device, VALUE hash = rb_hash_new())
    {
        return $self->create_output(device,
            hash_to_map_options(hash, $self->options()));
    }

    std::shared_ptr<sigrok::Output> _create_output(string filename,
        std::shared_ptr<sigrok::Device> device, VALUE hash = rb_hash_new())
    {
        return $self->create_output(filename, device,
            hash_to_map_options(hash, $self->options()));
    }
}

/* Support config_set() with Ruby input types. */
%extend sigrok::Configurable
{
    void config_set(const ConfigKey *key, VALUE input)
    {
        $self->config_set(key, ruby_to_variant_by_key(input, key));
    }
}

/* Return Ruby array from Analog::data(). */
%rename sigrok::Analog::_data data;
%extend sigrok::Analog
{
    VALUE _data()
    {
        int num_channels = $self->channels().size();
        int num_samples  = $self->num_samples();
        float *data = (float *) $self->data_pointer();
        VALUE channels = rb_ary_new2(num_channels);
        for(int i = 0; i < num_channels; i++) {
            VALUE samples = rb_ary_new2(num_samples);
            for (int j = 0; j < num_samples; j++) {
                rb_ary_store(samples, j, DBL2NUM(data[i*num_samples+j]));
            }
            rb_ary_store(channels, i, samples);
        }
        return channels;
    }
}
