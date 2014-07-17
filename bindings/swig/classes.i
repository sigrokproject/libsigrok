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

%{
#include "libsigrok/libsigrok.hpp"
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
%shared_ptr(sigrok::EventSource);
%shared_ptr(sigrok::Session);
%shared_ptr(sigrok::Packet);
%shared_ptr(sigrok::PacketPayload);
%shared_ptr(sigrok::Analog);
%shared_ptr(sigrok::Logic);
%shared_ptr(sigrok::InputFormat);
%shared_ptr(sigrok::InputFileDevice);
%shared_ptr(sigrok::OutputFormat);
%shared_ptr(sigrok::Output);
%shared_ptr(sigrok::Trigger);
%shared_ptr(sigrok::TriggerStage);
%shared_ptr(sigrok::TriggerMatch);

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

%template(QuantityFlagVector)
    std::vector<const sigrok::QuantityFlag *>;

%template(TriggerStageVector)
 std::vector<std::shared_ptr<sigrok::TriggerStage> >;

%template(TriggerMatchVector)
 std::vector<std::shared_ptr<sigrok::TriggerMatch> >;

#define SR_API
#define SR_PRIV

%ignore sigrok::DatafeedCallbackData;
%ignore sigrok::SourceCallbackData;

%include "libsigrok/libsigrok.hpp"

namespace sigrok {
%include "libsigrok/enums.hpp"
}

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
}

%attributeval(sigrok::Context,
    map_string_Driver, drivers, get_drivers);
%attributeval(sigrok::Context,
    map_string_InputFormat, input_formats, get_input_formats);
%attributeval(sigrok::Context,
    map_string_OutputFormat, output_formats, get_output_formats);

%attributestring(sigrok::Context,
    std::string, package_version, get_package_version);
%attributestring(sigrok::Context,
    std::string, lib_version, get_lib_version);

%attribute(sigrok::Context,
    const sigrok::LogLevel *, log_level, get_log_level, set_log_level);

%attributestring(sigrok::Context,
    std::string, log_domain, get_log_domain, set_log_domain);

%attributestring(sigrok::Driver, std::string, name, get_name);
%attributestring(sigrok::Driver, std::string, long_name, get_long_name);

%attributestring(sigrok::InputFormat,
    std::string, name, get_name);
%attributestring(sigrok::InputFormat,
    std::string, description, get_description);

%attributestring(sigrok::OutputFormat,
    std::string, name, get_name);
%attributestring(sigrok::OutputFormat,
    std::string, description, get_description);

%attributestring(sigrok::Device, std::string, vendor, get_vendor);
%attributestring(sigrok::Device, std::string, model, get_model);
%attributestring(sigrok::Device, std::string, version, get_version);

%attributeval(sigrok::Device,
    std::vector<std::shared_ptr<sigrok::Channel> >,
    channels, get_channels);

/* Using %attributestring for shared_ptr attribute. See
   http://sourceforge.net/p/swig/mailman/message/31832070/ */
%attributestring(sigrok::HardwareDevice,
    std::shared_ptr<sigrok::Driver>, driver, get_driver);

%attributeval(sigrok::HardwareDevice, map_string_ChannelGroup,
    channel_groups, get_channel_groups);

%attributestring(sigrok::Channel, std::string, name, get_name, set_name);
%attribute(sigrok::Channel, bool, enabled, get_enabled, set_enabled);
%attribute(sigrok::Channel, const sigrok::ChannelType *, type, get_type);

%attributestring(sigrok::ChannelGroup, std::string, name, get_name);
%attributeval(sigrok::ChannelGroup,
    std::vector<std::shared_ptr<sigrok::Channel> >,
    channels, get_channels);

%attributestring(sigrok::Trigger, std::string, name, get_name);
%attributeval(sigrok::Trigger,
    std::vector<std::shared_ptr<sigrok::TriggerStage> >,
    stages, get_stages);

%attribute(sigrok::TriggerStage, int, number, get_number);
%attributeval(sigrok::TriggerStage,
    std::vector<std::shared_ptr<sigrok::TriggerMatch> >,
    matches, get_matches);

%attributestring(sigrok::TriggerMatch,
    std::shared_ptr<sigrok::Channel>, channel, get_channel);
%attribute(sigrok::TriggerMatch, const sigrok::TriggerMatchType *, type, get_type);
%attribute(sigrok::TriggerMatch, float, value, get_value);

%attributeval(sigrok::Session,
    std::vector<std::shared_ptr<sigrok::Device> >,
    devices, get_devices);

%attribute(sigrok::Packet, sigrok::PacketPayload *, payload, get_payload);

%attribute(sigrok::Analog, int, num_samples, get_num_samples);
%attribute(sigrok::Analog, const sigrok::Quantity *, mq, get_mq);
%attribute(sigrok::Analog, const sigrok::Unit *, unit, get_unit);
%attributeval(sigrok::Analog, std::vector<const sigrok::QuantityFlag *>, mq_flags, get_mq_flags);
