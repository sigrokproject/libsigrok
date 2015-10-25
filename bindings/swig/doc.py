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
import sys, os

language, input_file = sys.argv[1:3]
if len(sys.argv) == 4:
    mode = sys.argv[3]
input_dir = os.path.dirname(input_file)

index = ElementTree.parse(input_file)

def get_text(node):
    paras = node.findall('para')
    return str.join('\n\n', [p.text.rstrip() for p in paras if p.text])

for compound in index.findall('compound'):
    if compound.attrib['kind'] != 'class':
        continue
    class_name = compound.find('name').text
    if not class_name.startswith('sigrok::'):
        continue
    trimmed_name = class_name.split('::')[1]
    doc = ElementTree.parse("%s/%s.xml" % (input_dir, compound.attrib['refid']))
    cls = doc.find('compounddef')
    brief = get_text(cls.find('briefdescription'))
    if brief:
        if language == 'python':
            print('%%feature("docstring") %s "%s";' % (class_name, brief))
        elif language == 'java':
            print('%%typemap(javaclassmodifiers) %s "/** %s */\npublic class"' % (
            class_name, brief))
    constants = []
    for section in cls.findall('sectiondef'):
        kind = section.attrib['kind']
        if kind not in ('public-func', 'public-static-attrib'):
            continue
        for member in section.findall('memberdef'):
            member_name = member.find('name').text
            brief = get_text(member.find('briefdescription')).replace('"', '\\"')
            parameters = {}
            for para in member.find('detaileddescription').findall('para'):
                paramlist = para.find('parameterlist')
                if paramlist is not None:
                    for param in paramlist.findall('parameteritem'):
                        namelist = param.find('parameternamelist')
                        name = namelist.find('parametername').text
                        description = get_text(param.find('parameterdescription'))
                        if description:
                            parameters[name] = description
            if brief:
                if language == 'python' and kind == 'public-func':
                    print(str.join('\n', [
                        '%%feature("docstring") %s::%s "%s' % (
                            class_name, member_name, brief)] + [
                        '@param %s %s' % (name, desc)
                            for name, desc in parameters.items()]) + '";')
                elif language == 'java' and kind == 'public-func':
                        print(str.join('\n', [
                            '%%javamethodmodifiers %s::%s "/** %s' % (
                                class_name, member_name, brief)] + [
                            '   * @param %s %s' % (name, desc)
                                for name, desc in parameters.items()])
                                    + ' */\npublic"')
                elif kind == 'public-static-attrib':
                    constants.append((member_name, brief))
    if language == 'java' and constants:
        print('%%typemap(javacode) %s %%{' % class_name)
        for member_name, brief in constants:
            print('  /** %s */\n  public static final %s %s = new %s(classesJNI.%s_%s_get(), false);\n' % (
                brief, trimmed_name, member_name, trimmed_name,
                trimmed_name, member_name))
        print('%}')
    elif language == 'python' and constants:
        if mode == 'start':
            print('%%extend %s {\n%%pythoncode %%{' % class_name)
            for member_name, brief in constants:
                print('    ## @brief %s\n    %s = None' % (brief, member_name))
            print('%}\n}')
        elif mode == 'end':
            print('%pythoncode %{')
            for member_name, brief in constants:
                print('%s.%s.__doc__ = """%s"""' % (
                    trimmed_name, member_name, brief))
            print('%}')
