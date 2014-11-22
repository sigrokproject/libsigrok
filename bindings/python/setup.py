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
import subprocess
import os

srcdir = os.path.split(__file__)[0]

sr_includes, sr_lib_dirs, sr_libs, (sr_version,) = [
    subprocess.check_output(
            ["pkg-config", option, "glib-2.0", "glibmm-2.4", "pygobject-3.0"]
        ).decode().rstrip().split(' ')
    for option in
        ("--cflags-only-I", "--libs-only-L", "--libs-only-l", "--modversion")]

includes = ['../../include', '../cxx/include']
includes += [os.path.join(srcdir, path) for path in includes]
includes += ['../..', '../../include/libsigrok', '../cxx/include/libsigrok']
includes += [i[2:] for i in sr_includes]
libdirs = ['../../.libs', '../cxx/.libs'] + [l[2:] for l in sr_lib_dirs]
libs = [l[2:] for l in sr_libs] + ['sigrokxx']

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
    version = sr_version,
    description = "libsigrok API wrapper",
    zip_safe = False,
    script_name = __file__,
    ext_modules = [
        Extension('sigrok.core._classes',
            sources = [vpath('sigrok/core/classes.i')],
            swig_opts = ['-c++', '-threads', '-Isigrok/core'] + 
                ['-I%s' % i for i in includes],
            extra_compile_args = ['-std=c++11', '-Wno-uninitialized'],
            include_dirs = includes,
            library_dirs = libdirs,
            libraries = libs)
    ],
    cmdclass = {'build_py': build_py, 'build_ext': build_ext},
)
