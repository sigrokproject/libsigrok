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

#pragma SWIG nowarn=325,401

%{
#include <libsigrokcxx/libsigrokcxx.hpp>
using namespace std;
%}

%include "typemaps.i"
%include "std_string.i"
%include "std_vector.i"
%include "std_map.i"
%include "std_shared_ptr.i"
%include "exception.i"

%{

static int swig_exception_code(int sigrok_exception_code) {
    switch (sigrok_exception_code) {
        case SR_ERR_MALLOC:
            return SWIG_MemoryError;
        case SR_ERR_ARG:
            return SWIG_ValueError;
        default:
            return SWIG_RuntimeError;
    }
}

%}

%exception {
    try {
        $action
    } catch (sigrok::Error &e) {
        SWIG_exception(swig_exception_code(e.result),
            const_cast<char*>(e.what()));
    }
}

template< class T > class enable_shared_from_this;

%template(ContextShared) enable_shared_from_this<Context>;

%shared_ptr(sigrok::Context);
%shared_ptr(sigrok::Driver);
%shared_ptr(sigrok::Device);
%shared_ptr(sigrok::Configurable);
%shared_ptr(sigrok::HardwareDevice);
%shared_ptr(sigrok::Channel);
%shared_ptr(sigrok::ChannelGroup);
%shared_ptr(sigrok::Session);
%shared_ptr(sigrok::SessionDevice);
%shared_ptr(sigrok::Packet);
%shared_ptr(sigrok::PacketPayload);
%shared_ptr(sigrok::Header);
%shared_ptr(sigrok::Meta);
%shared_ptr(sigrok::AnalogOld);
%shared_ptr(sigrok::Logic);
%shared_ptr(sigrok::InputFormat);
%shared_ptr(sigrok::Input);
%shared_ptr(sigrok::InputDevice);
%shared_ptr(sigrok::Option);
%shared_ptr(sigrok::OutputFormat);
%shared_ptr(sigrok::Output);
%shared_ptr(sigrok::Trigger);
%shared_ptr(sigrok::TriggerStage);
%shared_ptr(sigrok::TriggerMatch);
%shared_ptr(sigrok::UserDevice);

%template(StringMap) std::map<std::string, std::string>;

%template(DriverMap)
    std::map<std::string, std::shared_ptr<sigrok::Driver> >;
%template(InputFormatMap)
    std::map<std::string, std::shared_ptr<sigrok::InputFormat> >;
%template(OutputFormatMap)
    std::map<std::string, std::shared_ptr<sigrok::OutputFormat> >;

%template(HardwareDeviceVector)
    std::vector<std::shared_ptr<sigrok::HardwareDevice> >;

%template(DeviceVector)
    std::vector<std::shared_ptr<sigrok::Device> >;

%template(ChannelVector)
    std::vector<std::shared_ptr<sigrok::Channel> >;

%template(ChannelGroupMap)
    std::map<std::string, std::shared_ptr<sigrok::ChannelGroup> >;

/* Workaround for SWIG bug. The vector template instantiation
   isn't needed but somehow fixes a bug that stops the wrapper
   for the map instantiation from compiling. */
%template(ConfigVector)
    std::vector<const sigrok::ConfigKey *>;
%template(ConfigMap)
    std::map<const sigrok::ConfigKey *, Glib::VariantBase>;

%template(OptionVector)
    std::vector<std::shared_ptr<sigrok::Option> >;
%template(OptionMap)
    std::map<std::string, std::shared_ptr<sigrok::Option> >;

%template(VariantVector)
    std::vector<Glib::VariantBase>;
%template(VariantMap)
    std::map<std::string, Glib::VariantBase>;

%template(QuantityFlagVector)
    std::vector<const sigrok::QuantityFlag *>;

%template(TriggerStageVector)
 std::vector<std::shared_ptr<sigrok::TriggerStage> >;

%template(TriggerMatchVector)
 std::vector<std::shared_ptr<sigrok::TriggerMatch> >;

#define SR_API
#define SR_PRIV

%ignore sigrok::DatafeedCallbackData;

#define SWIG_ATTRIBUTE_TEMPLATE

%include "attribute.i"

%inline {
typedef std::map<std::string, std::shared_ptr<sigrok::Driver> >
    map_string_Driver;
typedef std::map<std::string, std::shared_ptr<sigrok::InputFormat> >
    map_string_InputFormat;
typedef std::map<std::string, std::shared_ptr<sigrok::OutputFormat> >
    map_string_OutputFormat;
typedef std::map<std::string, std::shared_ptr<sigrok::ChannelGroup> >
    map_string_ChannelGroup;
typedef std::map<std::string, std::shared_ptr<sigrok::Option> >
    map_string_Option;
typedef std::map<std::string, Glib::VariantBase>
    map_string_Variant;
typedef std::map<const sigrok::ConfigKey *, Glib::VariantBase>
    map_ConfigKey_Variant;
}

%attributemap(Context,
    map_string_Driver, drivers, drivers);
%attributemap(Context,
    map_string_InputFormat, input_formats, input_formats);
%attributemap(Context,
    map_string_OutputFormat, output_formats, output_formats);

%attributestring(sigrok::Context,
    std::string, package_version, package_version);
%attributestring(sigrok::Context,
    std::string, lib_version, lib_version);

%attribute(sigrok::Context,
    const sigrok::LogLevel *, log_level, log_level, set_log_level);

%attributestring(sigrok::Driver, std::string, name, name);
%attributestring(sigrok::Driver, std::string, long_name, long_name);

%attributestring(sigrok::InputFormat,
    std::string, name, name);
%attributestring(sigrok::InputFormat,
    std::string, description, description);

%attributestring(sigrok::Input,
    std::shared_ptr<sigrok::InputDevice>, device, device);

%attributestring(sigrok::Option,
    std::string, id, id);
%attributestring(sigrok::Option,
    std::string, name, name);
%attributestring(sigrok::Option,
    std::string, description, description);
/* Currently broken on Python due to some issue with variant typemaps. */
/* %attributevector(Option,
    Glib::VariantBase, default_value, default_value); */
%attributevector(Option,
    std::vector<Glib::VariantBase>, values, values);

%attributestring(sigrok::OutputFormat,
    std::string, name, name);
%attributestring(sigrok::OutputFormat,
    std::string, description, description);
%attributemap(OutputFormat,
    map_string_Option, options, options);

%attributestring(sigrok::Device, std::string, vendor, vendor);
%attributestring(sigrok::Device, std::string, model, model);
%attributestring(sigrok::Device, std::string, version, version);

%attributevector(Device,
    std::vector<std::shared_ptr<sigrok::Channel> >,
    channels, channels);

%attributemap(Device, map_string_ChannelGroup,
    channel_groups, channel_groups);

/* Using %attributestring for shared_ptr attribute. See
   http://sourceforge.net/p/swig/mailman/message/31832070/ */
%attributestring(sigrok::HardwareDevice,
    std::shared_ptr<sigrok::Driver>, driver, driver);

%attributestring(sigrok::Channel, std::string, name, name, set_name);
%attribute(sigrok::Channel, bool, enabled, enabled, set_enabled);
%attribute(sigrok::Channel, const sigrok::ChannelType *, type, type);
%attribute(sigrok::Channel, unsigned int, index, index);

%attributestring(sigrok::ChannelGroup, std::string, name, name);
%attributevector(ChannelGroup,
    std::vector<std::shared_ptr<sigrok::Channel> >,
    channels, channels);

%attributestring(sigrok::Trigger, std::string, name, name);
%attributevector(Trigger,
    std::vector<std::shared_ptr<sigrok::TriggerStage> >,
    stages, stages);

%attribute(sigrok::TriggerStage, int, number, number);
%attributevector(TriggerStage,
    std::vector<std::shared_ptr<sigrok::TriggerMatch> >,
    matches, matches);

%attributestring(sigrok::TriggerMatch,
    std::shared_ptr<sigrok::Channel>, channel, channel);
%attribute(sigrok::TriggerMatch, const sigrok::TriggerMatchType *, type, type);
%attribute(sigrok::TriggerMatch, float, value, value);

%attributevector(Session,
    std::vector<std::shared_ptr<sigrok::Device> >,
    devices, devices);

%attributestring(sigrok::Session,
    std::shared_ptr<sigrok::Trigger>, trigger, trigger, set_trigger);

%attributestring(sigrok::Session, std::string, filename, filename);

%attribute(sigrok::Packet,
    const sigrok::PacketType *, type, type);

%attributemap(Meta, map_ConfigKey_Variant, config, config);

%attributevector(AnalogOld,
    std::vector<std::shared_ptr<sigrok::Channel> >, channels, channels);
%attribute(sigrok::AnalogOld, int, num_samples, num_samples);
%attribute(sigrok::AnalogOld, const sigrok::Quantity *, mq, mq);
%attribute(sigrok::AnalogOld, const sigrok::Unit *, unit, unit);
%attributevector(AnalogOld, std::vector<const sigrok::QuantityFlag *>, mq_flags, mq_flags);

%include <libsigrokcxx/libsigrokcxx.hpp>

%include "swig/enums.i"

namespace sigrok {
%include <libsigrokcxx/enums.hpp>
}
