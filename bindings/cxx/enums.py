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

from xml.etree import ElementTree
from collections import OrderedDict
import sys, os, re

index_file = sys.argv[1]

# Get directory this script is in.
dirname = os.path.dirname(os.path.realpath(__file__))

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

index = ElementTree.parse(index_file)

# Build mapping between class names and enumerations.

classes = OrderedDict()

for compound in index.findall('compound'):
    if compound.attrib['kind'] != 'file':
        continue
    filename = os.path.join(
        os.path.dirname(index_file),
        '%s.xml' % compound.attrib['refid'])
    doc = ElementTree.parse(filename)
    for section in doc.find('compounddef').findall('sectiondef'):
        if section.attrib["kind"] != 'enum':
            continue
        for member in section.findall('memberdef'):
            if member.attrib["kind"] != 'enum':
                continue
            name = member.find('name').text
            if name in mapping:
                classes[member] = mapping[name]

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

for enum, classname in classes.items():

    enum_name = enum.find('name').text
    member_names = [m.find('name').text for m in enum.findall('enumvalue')]
    trimmed_names = [re.sub("^SR_[A-Z]+_", "", n) for n in member_names]

    # Begin class and public declarations
    print >> header, header_public_template.format(
        classname=classname, enumname=enum_name)

    # Declare public pointers for each enum value
    for trimmed_name in trimmed_names:
        print >> header, '\tstatic const %s * const %s;' % (
            classname, trimmed_name)

    # Declare additional methods if present
    filename = os.path.join(dirname, "%s_methods.hpp" % classname)
    if os.path.exists(filename):
        print >> header, str.join('', open(filename).readlines())

    # Begin private declarations
    print >> header, header_private_template.format(
        classname=classname, enumname=enum_name)

    # Declare private constants for each enum value
    for trimmed_name in trimmed_names:
        print >> header, '\tstatic const %s _%s;' % (classname, trimmed_name)

    # End class declaration
    print >> header, '};'

    # Begin class code
    print >> code, code_template.format(
        classname=classname, enumname=enum_name)

    # Define private constants for each enum value
    for name, trimmed_name in zip(member_names, trimmed_names):
        print >> code, 'const %s %s::_%s = %s(%s, "%s");' % (
            classname, classname, trimmed_name, classname, name, trimmed_name)

    # Define public pointers for each enum value
    for trimmed_name in trimmed_names:
        print >> code, 'const %s * const %s::%s = &%s::_%s;' % (
            classname, classname, trimmed_name, classname, trimmed_name)

    # Define map of enum values to constants
    print >> code, 'const std::map<enum %s, const %s *> %s::values = {' % (
        enum_name, classname, classname)
    for name, trimmed_name in zip(member_names, trimmed_names):
        print >> code, '\t{%s, %s::%s},' % (name, classname, trimmed_name)
    print >> code, '};'

    # Define additional methods if present
    filename = os.path.join(dirname, "%s_methods.cpp" % classname)
    if os.path.exists(filename):
        print >> code, str.join('', open(filename).readlines())
