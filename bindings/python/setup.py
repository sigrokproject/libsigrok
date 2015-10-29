##
## This file is part of the libsigrok project.
##
## Copyright (C) 2013 Martin Ling <martin-sigrok@earth.li>
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

from setuptools import setup, find_packages, Extension
from distutils.command.build_py import build_py as _build_py
from distutils.command.build_ext import build_ext as _build_ext
import numpy as np
import os
import sys
import re
import shlex

srcdir = os.path.dirname(os.path.abspath(__file__))
os.chdir('bindings/python')
srcdir = os.path.relpath(srcdir)
srcdir_parent = os.path.normpath(os.path.join(srcdir, '..'))

# Override the default compile flags used by distutils.
os.environ['OPT'] = ''

# Parse the command line arguments for VAR=value assignments,
# and apply them as environment variables.
while len(sys.argv) > 1:
    match = re.match(r'([A-Z]+)=(.*)', sys.argv[1])
    if match is None:
        break
    os.environ[match.group(1)] = match.group(2)
    del sys.argv[1]

includes = ['../../include', '../cxx/include']
includes += [os.path.normpath(os.path.join(srcdir, path)) for path in includes]
includes += ['../..', np.get_include()]

ldadd = shlex.split(os.environ.get('LDADD', ''))
libdirs = ['../../.libs', '../cxx/.libs'] + \
    [l[2:] for l in ldadd if l.startswith('-L')]
libs = [l[2:] for l in ldadd if l.startswith('-l')] + ['sigrokcxx']

def vpath(file):
    vfile = os.path.join(srcdir, file)
    return vfile if os.path.exists(vfile) else file

def unvpath(file):
    return os.path.relpath(file, srcdir) if file.startswith(srcdir) else file

class build_py(_build_py):
    def find_package_modules(self, package, pkg_dir):
        mods = _build_py.find_package_modules(self, package, pkg_dir)
        vmods = _build_py.find_package_modules(self, package, vpath(pkg_dir))
        mods.extend([mod for mod in vmods if mod not in mods])
        return mods
    def check_package(self, package, package_dir):
        return _build_py.check_package(self, package, vpath(package_dir))

class build_ext(_build_ext):
    def finalize_options(self):
        _build_ext.finalize_options(self)
        self.swig_opts = ['-c++', '-threads', '-Isigrok/core', '-I..',
            '-I' + srcdir_parent] + ['-I%s' % i for i in includes] + self.swig_opts
    def spawn (self, cmd):
        cmd[1:-1] = [arg if arg.startswith('-') else unvpath(arg) for arg in
                     cmd[1:-1]]
        _build_ext.spawn(self, cmd)
    def swig_sources (self, sources, extension):
        return [unvpath(src) for src in
                _build_ext.swig_sources(self, sources, extension)]

setup(
    name = 'libsigrok',
    namespace_packages = ['sigrok'],
    packages = find_packages(srcdir),
    version = os.environ.get('VERSION'),
    description = "libsigrok API wrapper",
    zip_safe = False,
    ext_modules = [
        Extension('sigrok.core._classes',
            sources = [vpath('sigrok/core/classes.i')],
            extra_compile_args = ['-Wno-uninitialized'],
            include_dirs = includes,
            library_dirs = libdirs,
            libraries = libs)
    ],
    cmdclass = {'build_py': build_py, 'build_ext': build_ext},
)
