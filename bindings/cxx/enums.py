##
## This file is part of the libsigrok project.
##
## Copyright (C) 2014 Martin Ling <martin-sigrok@earth.li>
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.
##

import logging, warnings
from pygccxml import parser, declarations
from collections import OrderedDict
import sys, os, re

class crapfilter(logging.Filter):
    def filter(self, record):
        if record.msg.find('GCCXML version') > -1:
            return 0
        return 1
logger = logging.getLogger('pygccxml.cxx_parser').addFilter(crapfilter())
warnings.filterwarnings('ignore', message="unable to find out array size from expression")

# Get directory this script is in.
dirname = os.path.dirname(os.path.realpath(__file__))

# Parse GCCXML output to get declaration tree.
decls = parser.parse_xml_file(
    os.path.join(dirname, "libsigrok.xml"), parser.config_t())

# Get global namespace from declaration tree.
ns = declarations.get_global_namespace(decls)

mapping = dict([
    ('sr_loglevel', 'LogLevel'),
    ('sr_packettype', 'PacketType'),
    ('sr_mq', 'Quantity'),
    ('sr_unit', 'Unit'),
    ('sr_mqflag', 'QuantityFlag'),
    ('sr_configkey', 'ConfigKey'),
    ('sr_datatype', 'DataType'),
    ('sr_channeltype', 'ChannelType'),
    ('sr_trigger_matches', 'TriggerMatchType')])

classes = OrderedDict()

# Build mapping between class names and enumerations.
for enum in ns.enumerations():
    if enum.name in mapping:
        classname = mapping[enum.name]
        classes[classname] = enum

header = open(os.path.join(dirname, 'include/libsigrok/enums.hpp'), 'w')
code = open(os.path.join(dirname, 'enums.cpp'), 'w')

for file in (header, code):
    print >> file, "/* Generated file - edit enums.py instead! */"

# Template for beginning of class declaration and public members.
header_public_template = """
class SR_API {classname} : public EnumValue<enum {enumname}>
{{
public:
    static const {classname} *get(int id);
"""

# Template for beginning of private members.
header_private_template = """
private:
    static const std::map<enum {enumname}, const {classname} *> values;
    {classname}(enum {enumname} id, const char name[]);
"""

# Template for class method definitions.
code_template = """
{classname}::{classname}(enum {enumname} id, const char name[]) :
    EnumValue<enum {enumname}>(id, name)
{{
}}

const {classname} *{classname}::get(int id)
{{
    return {classname}::values.at(static_cast<{enumname}>(id));
}}
"""

for classname, enum in classes.items():

    # Begin class and public declarations
    print >> header, header_public_template.format(
        classname=classname, enumname=enum.name)

    # Declare public pointers for each enum value
    for name, value in enum.values:
        trimmed_name = re.sub("^SR_[A-Z]+_", "", name)
        print >> header, '\tstatic const %s * const %s;' % (classname, trimmed_name)

    # Declare additional methods if present
    filename = os.path.join(dirname, "%s_methods.hpp" % classname)
    if os.path.exists(filename):
        print >> header, str.join('', open(filename).readlines())

    # Begin private declarations
    print >> header, header_private_template.format(
        classname=classname, enumname=enum.name)

    # Declare private constants for each enum value
    for name, value in enum.values:
        trimmed_name = re.sub("^SR_[A-Z]+_", "", name)
        print >> header, '\tstatic const %s _%s;' % (classname, trimmed_name)

    # End class declaration
    print >> header, '};'

    # Begin class code
    print >> code, code_template.format(
        classname=classname, enumname=enum.name)

    # Define private constants for each enum value
    for name, value in enum.values:
        trimmed_name = re.sub("^SR_[A-Z]+_", "", name)
        print >> code, 'const %s %s::_%s = %s(%s, "%s");' % (
            classname, classname, trimmed_name, classname, name, trimmed_name)

    # Define public pointers for each enum value
    for name, value in enum.values:
        trimmed_name = re.sub("^SR_[A-Z]+_", "", name)
        print >> code, 'const %s * const %s::%s = &%s::_%s;' % (
            classname, classname, trimmed_name, classname, trimmed_name)

    # Define map of enum values to constants
    print >> code, 'const std::map<enum %s, const %s *> %s::values = {' % (
        enum.name, classname, classname)
    for name, value in enum.values:
        trimmed_name = re.sub("^SR_[A-Z]+_", "", name)
        print >> code, '\t{%s, %s::%s},' % (name, classname, trimmed_name)
    print >> code, '};'

    # Define additional methods if present
    filename = os.path.join(dirname, "%s_methods.cpp" % classname)
    if os.path.exists(filename):
        print >> code, str.join('', open(filename).readlines())
