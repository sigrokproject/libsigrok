/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Martin Ling <martin-sigrok@earth.li>
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
#include <libsigrokcxx/libsigrokcxx.hpp>
using namespace std;
%}

%include "std_string.i"
%include "std_shared_ptr.i"
%include "std_vector.i"
%include "std_map.i"

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
