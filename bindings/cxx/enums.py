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

from __future__ import print_function
from xml.etree import ElementTree
from collections import OrderedDict
import sys, os, re

index_file = sys.argv[1]

# Get directory this script is in.
dirname = os.path.dirname(os.path.realpath(__file__))

outdirname = "bindings"
if not os.path.exists(os.path.join(outdirname, 'cxx/include/libsigrokcxx')):
    os.makedirs(os.path.join(outdirname, 'cxx/include/libsigrokcxx'))
if not os.path.exists(os.path.join(outdirname, 'swig')):
    os.makedirs(os.path.join(outdirname, 'swig'))

mapping = dict([
    ('sr_loglevel', ('LogLevel', 'Log verbosity level')),
    ('sr_packettype', ('PacketType', 'Type of datafeed packet')),
    ('sr_mq', ('Quantity', 'Measured quantity')),
    ('sr_unit', ('Unit', 'Unit of measurement')),
    ('sr_mqflag', ('QuantityFlag', 'Flag applied to measured quantity')),
    ('sr_configkey', ('ConfigKey', 'Configuration key')),
    ('sr_datatype', ('DataType', 'Configuration data type')),
    ('sr_channeltype', ('ChannelType', 'Channel type')),
    ('sr_trigger_matches', ('TriggerMatchType', 'Trigger match type')),
    ('sr_output_flag', ('OutputFlag', 'Flag applied to output modules'))])

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

header = open(os.path.join(outdirname, 'cxx/include/libsigrokcxx/enums.hpp'), 'w')
code = open(os.path.join(outdirname, 'cxx/enums.cpp'), 'w')
swig = open(os.path.join(outdirname, 'swig/enums.i'), 'w')

for file in (header, code):
    print("/* Generated file - edit enums.py instead! */", file=file)

# Template for beginning of class declaration and public members.
header_public_template = """
/** {brief} */
class SR_API {classname} : public EnumValue<{classname}, enum {enumname}>
{{
public:
"""

# Template for beginning of private members.
header_private_template = """
protected:
    {classname}(enum {enumname} id, const char name[]) : EnumValue(id, name) {{}}
"""

def get_text(node):
    return str.join('\n\n',
        [p.text.rstrip() for p in node.findall('para')])

for enum, (classname, classbrief) in classes.items():

    enum_name = enum.find('name').text
    members = enum.findall('enumvalue')
    member_names = [m.find('name').text for m in members]
    trimmed_names = [re.sub("^SR_[A-Z]+_", "", n) for n in member_names]
    briefs = [get_text(m.find('briefdescription')) for m in members]

    # Begin class and public declarations
    print(header_public_template.format(
        brief=classbrief, classname=classname, enumname=enum_name), file=header)

    # Declare public pointers for each enum value
    for trimmed_name, brief in zip(trimmed_names, briefs):
        if brief:
            print('\t/** %s */' % brief, file=header)
        print('\tstatic const %s * const %s;' % (
            classname, trimmed_name), file=header)

    # Declare additional methods if present
    filename = os.path.join(dirname, "%s_methods.hpp" % classname)
    if os.path.exists(filename):
        print(str.join('', open(filename).readlines()), file=header)

    # Begin private declarations
    print(header_private_template.format(
        classname=classname, enumname=enum_name), file=header)

    # Declare private constants for each enum value
    for trimmed_name in trimmed_names:
        print('\tstatic const %s _%s;' % (classname, trimmed_name), file=header)

    # End class declaration
    print('};', file=header)

    # Define private constants for each enum value
    for name, trimmed_name in zip(member_names, trimmed_names):
        print('const %s %s::_%s = %s(%s, "%s");' % (
            classname, classname, trimmed_name, classname, name, trimmed_name),
            file=code)

    # Define public pointers for each enum value
    for trimmed_name in trimmed_names:
        print('const %s * const %s::%s = &%s::_%s;' % (
            classname, classname, trimmed_name, classname, trimmed_name),
            file=code)

    # Define map of enum values to constants
    print('template<> const SR_API std::map<const enum %s, const %s * const> EnumValue<%s, enum %s>::_values = {' % (
        enum_name, classname, classname, enum_name), file=code)
    for name, trimmed_name in zip(member_names, trimmed_names):
        print('\t{%s, %s::%s},' % (name, classname, trimmed_name), file=code)
    print('};', file=code)

    # Define additional methods if present
    filename = os.path.join(dirname, "%s_methods.cpp" % classname)
    if os.path.exists(filename):
        print(str.join('', open(filename).readlines()), file=code)

    # Map EnumValue::id() and EnumValue::name() as SWIG attributes.
    print('%%attribute(sigrok::%s, int, id, id);' % classname, file=swig)
    print('%%attributestring(sigrok::%s, std::string, name, name);' % classname,
        file=swig)

    # Instantiate EnumValue template for SWIG
    print('%%template(EnumValue%s) sigrok::EnumValue<sigrok::%s, enum %s>;' % (
        classname, classname, enum_name), file=swig)

    # Apply any language-specific extras.
    print('%%enumextras(%s);' % classname, file=swig)

    # Declare additional attributes if present
    filename = os.path.join(dirname, "%s_methods.i" % classname)
    if os.path.exists(filename):
        print(str.join('', open(filename).readlines()), file=swig)
