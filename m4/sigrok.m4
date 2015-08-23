## Copyright (c) 2009  Openismus GmbH
## Copyright (c) 2015  Daniel Elstner <daniel.kitta@gmail.com>
##
## This file is part of the sigrok project.
##
## sigrok is free software: you can redistribute it and/or modify it
## under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## sigrok is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with sigrok. If not, see <http://www.gnu.org/licenses/>.

#serial 20150821

## SR_APPEND(var-name, [list-sep,] element)
##
## Append the shell word <element> to the shell variable named <var-name>,
## prefixed by <list-sep> unless the list was empty before appending. If
## only two arguments are supplied, <list-sep> defaults to a single space
## character.
##
AC_DEFUN([SR_APPEND],
[dnl
m4_assert([$# >= 2])[]dnl
$1=[$]{$1[}]m4_if([$#], [2], [[$]{$1:+' '}$2], [[$]{$1:+$2}$3])[]dnl
])

## _SR_PKG_VERSION_SET(var-prefix, pkg-name, tag-prefix, base-version, major, minor, [micro])
##
m4_define([_SR_PKG_VERSION_SET],
[dnl
m4_assert([$# >= 6])[]dnl
$1=$4
sr_git_deps=
# Check if we can get revision information from git.
sr_head=`git -C "$srcdir" rev-parse --verify --short HEAD 2>&AS_MESSAGE_LOG_FD`

AS_IF([test "$?" = 0 && test "x$sr_head" != x], [dnl
	test ! -f "$srcdir/.git/HEAD" \
		|| sr_git_deps="$sr_git_deps \$(top_srcdir)/.git/HEAD"

	sr_head_name=`git -C "$srcdir" rev-parse --symbolic-full-name HEAD 2>&AS_MESSAGE_LOG_FD`
	AS_IF([test "$?" = 0 && test -f "$srcdir/.git/$sr_head_name"],
		[sr_git_deps="$sr_git_deps \$(top_srcdir)/.git/$sr_head_name"])

	# Append the revision hash unless we are exactly on a tagged release.
	git -C "$srcdir" describe --match "$3$4" \
		--exact-match >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD \
		|| $1="[$]$1-git-$sr_head"
])
# Use $(wildcard) so that things do not break if for whatever
# reason these files do not exist anymore at make time.
AS_IF([test -n "$sr_git_deps"],
	[SR_APPEND([CONFIG_STATUS_DEPENDENCIES], ["\$(wildcard$sr_git_deps)"])])
AC_SUBST([CONFIG_STATUS_DEPENDENCIES])[]dnl
AC_SUBST([$1])[]dnl
dnl
AC_DEFINE([$1_MAJOR], [$5], [Major version number of $2.])[]dnl
AC_DEFINE([$1_MINOR], [$6], [Minor version number of $2.])[]dnl
m4_ifval([$7], [AC_DEFINE([$1_MICRO], [$7], [Micro version number of $2.])])[]dnl
AC_DEFINE_UNQUOTED([$1_STRING], ["[$]$1"], [Version of $2.])[]dnl
])

## SR_PKG_VERSION_SET(var-prefix, version-triple)
##
## Set up substitution variables and macro definitions for the package
## version components. Derive the version suffix from the repository
## revision if possible.
##
## Substitutions: <var-prefix>
## Macro defines: <var-prefix>_{MAJOR,MINOR,MICRO,STRING}
##
AC_DEFUN([SR_PKG_VERSION_SET],
[dnl
m4_assert([$# >= 2])[]dnl
_SR_PKG_VERSION_SET([$1],
	m4_defn([AC_PACKAGE_NAME]),
	m4_defn([AC_PACKAGE_TARNAME])[-],
	m4_expand([$2]),
	m4_unquote(m4_split(m4_expand([$2]), [\.])))
])

## _SR_LIB_VERSION_SET(var-prefix, pkg-name, abi-triple, current, revision, age)
##
m4_define([_SR_LIB_VERSION_SET],
[dnl
m4_assert([$# >= 6])[]dnl
$1=$3
AC_SUBST([$1])[]dnl
AC_DEFINE([$1_CURRENT], [$4], [Binary version of $2.])[]dnl
AC_DEFINE([$1_REVISION], [$5], [Binary revision of $2.])[]dnl
AC_DEFINE([$1_AGE], [$6], [Binary age of $2.])[]dnl
AC_DEFINE([$1_STRING], ["$3"], [Binary version triple of $2.])[]dnl
])

## SR_LIB_VERSION_SET(var-prefix, abi-triple)
##
## Set up substitution variables and macro definitions for a library
## binary version.
##
## Substitutions: <var-prefix>
## Macro defines: <var-prefix>_{CURRENT,REVISION,AGE,STRING}
##
AC_DEFUN([SR_LIB_VERSION_SET],
[dnl
m4_assert([$# >= 1])[]dnl
_SR_LIB_VERSION_SET([$1],
	m4_defn([AC_PACKAGE_NAME]),
	[$2], m4_unquote(m4_split([$2], [:])))
])

## _SR_ARG_ENABLE_WARNINGS_ONCE
##
## Implementation helper macro of SR_ARG_ENABLE_WARNINGS. Pulled in
## through AC_REQUIRE so that it is only expanded once.
##
m4_define([_SR_ARG_ENABLE_WARNINGS_ONCE],
[dnl
AC_PROVIDE([$0])[]dnl
AC_ARG_ENABLE([warnings],
		[AS_HELP_STRING([[--enable-warnings[=min|max|fatal|no]]],
				[set compile pedantry level [default=min]])],
		[sr_enable_warnings=$enableval],
		[sr_enable_warnings=min])[]dnl
dnl
# Test whether the compiler accepts each flag.  Look at standard output,
# since GCC only shows a warning message if an option is not supported.
sr_check_compile_warning_flags() {
	for sr_flag
	do
		sr_cc_out=`$sr_cc $sr_warning_flags $sr_flag -c "$sr_conftest" 2>&1 || echo failed`
		AS_IF([test "$?$sr_cc_out" = 0],
			[SR_APPEND([sr_warning_flags], [$sr_flag])],
			[AS_ECHO(["$sr_cc: $sr_cc_out"]) >&AS_MESSAGE_LOG_FD])
		rm -f "conftest.[$]{OBJEXT:-o}"
	done
}
])

## SR_ARG_ENABLE_WARNINGS(variable, min-flags, max-flags)
##
## Provide the --enable-warnings configure argument, set to "min" by default.
## <min-flags> and <max-flags> should be space-separated lists of compiler
## warning flags to use with --enable-warnings=min or --enable-warnings=max,
## respectively. Warning level "fatal" is the same as "max" but in addition
## enables -Werror mode.
##
## In order to determine the warning options to use with the C++ compiler,
## call AC_LANG([C++]) first to change the current language. If different
## output variables are used, it is also fine to call SR_ARG_ENABLE_WARNINGS
## repeatedly, once for each language setting.
##
AC_DEFUN([SR_ARG_ENABLE_WARNINGS],
[dnl
m4_assert([$# >= 3])[]dnl
AC_REQUIRE([_SR_ARG_ENABLE_WARNINGS_ONCE])[]dnl
dnl
AS_CASE([$ac_compile],
	[[*'$CXXFLAGS '*]], [sr_lang='C++' sr_cc=$CXX sr_conftest="conftest.[$]{ac_ext:-cc}"],
	[[*'$CFLAGS '*]],   [sr_lang=C sr_cc=$CC sr_conftest="conftest.[$]{ac_ext:-c}"],
	[AC_MSG_ERROR([[current language is neither C nor C++]])])
dnl
AC_MSG_CHECKING([which $sr_lang compiler warning flags to use])
sr_warning_flags=
AC_LANG_CONFTEST([AC_LANG_SOURCE([[
int main(int argc, char** argv) { return (argv != 0) ? argc : 0; }
]])])
AS_CASE([$sr_enable_warnings],
	[no], [],
	[max], [sr_check_compile_warning_flags $3],
	[fatal], [sr_check_compile_warning_flags $3 -Werror],
	[sr_check_compile_warning_flags $2])
rm -f "$sr_conftest"
AC_SUBST([$1], [$sr_warning_flags])
AC_MSG_RESULT([[$]{sr_warning_flags:-none}])[]dnl
])
