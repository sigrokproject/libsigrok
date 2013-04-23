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
import subprocess

sr_includes, sr_lib_dirs, sr_libs, (sr_version,) = [
    subprocess.check_output(
        ["pkg-config", option, "libsigrok"]).decode().rstrip().split(' ')
    for option in
        ("--cflags-only-I", "--libs-only-L", "--libs-only-l", "--modversion")]

setup(
    name = 'libsigrok',
    namespace_packages = ['sigrok'],
    packages = find_packages(),
    version = sr_version,
    description = "libsigrok API wrapper",
    ext_modules = [
        Extension('sigrok.core._lowlevel',
            sources = ['sigrok/core/lowlevel.i'],
            swig_opts = ['-threads'] + sr_includes,
            include_dirs = [i[2:] for i in sr_includes],
            library_dirs = [l[2:] for l in sr_lib_dirs],
            libraries = [l[2:] for l in sr_libs]
        )
    ],
)
