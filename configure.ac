# This file is part of GDBM.                                   -*- autoconf -*-
# Copyright (C) 2007-2022 Free Software Foundation, Inc.
#
# GDBM is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# GDBM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GDBM. If not, see <http://www.gnu.org/licenses/>. */

m4_define([_GDBM_VERSION_MAJOR], 1)
m4_define([_GDBM_VERSION_MINOR], 23)

AC_INIT([gdbm],
        _GDBM_VERSION_MAJOR._GDBM_VERSION_MINOR[]m4_ifdef([_GDBM_VERSION_PATCH],._GDBM_VERSION_PATCH),
	[bug-gdbm@gnu.org],,
	[http://www.gnu.org/software/gdbm])

dnl When switching to 2.70, see Internationalization macros below
AC_PREREQ([2.69]) 
AC_CONFIG_SRCDIR([src/gdbmdefs.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([autoconf.h])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([gnits 1.11 std-options silent-rules])

# Enable silent rules by default:
AM_SILENT_RULES([yes])

AC_SUBST([GDBM_VERSION_MAJOR], _GDBM_VERSION_MAJOR)
AC_SUBST([GDBM_VERSION_MINOR], _GDBM_VERSION_MINOR)
AC_SUBST([GDBM_VERSION_PATCH], m4_ifdef([_GDBM_VERSION_PATCH],_GDBM_VERSION_PATCH,0))

AC_ARG_ENABLE([memory-mapped-io],
  AS_HELP_STRING(
    [--enable-memory-mapped-io],
    [Use mmap(2) for disk I/O. (Default is YES.)]),
  [mapped_io=$enableval],
  [mapped_io=yes])

AC_ARG_ENABLE([libgdbm-compat],
  AS_HELP_STRING(
    [--enable-libgdbm-compat],
    [Build and install libgdbm_compat. (Default is NO.)]),
  [want_compat=$enableval],
  [want_compat=no])
AC_ARG_VAR([COMPATINCLUDEDIR],[installation directory for dbm.h and ndbm.h])
test -z "$COMPATINCLUDEDIR" && COMPATINCLUDEDIR='$(includedir)'

AC_ARG_ENABLE([gdbmtool-debug],
  AS_HELP_STRING(
    [--enable-gdbmtool-debug],
    [instrument gdbmtool for additional debugging]),
    [want_gdbmtool_debug=$enableval],
    [want_gdbmtool_debug=default])

dnl Check for programs
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
LT_INIT
AC_PATH_PROG([BISON],[bison],[\$(SHELL) \$(top_srcdir)/build-aux/missing bison])
AC_PATH_PROG([FLEX],[flex],[\$(SHELL) \$(top_srcdir)/build-aux/missing flex])

AC_ARG_VAR([GZIP_BIN],[Absolute filename of the gzip binary])
AC_PATH_PROG(GZIP_BIN,[gzip])
AC_ARG_VAR([BASE64_BIN],[Absolute filename of the base64 binary])
AC_PATH_PROG(BASE64_BIN,[base64])

AC_SYS_LARGEFILE
AC_C_CONST
AC_TYPE_UNSIGNED_LONG_LONG_INT

AC_SUBST(GDBM_COUNT_T)
if test $ac_cv_type_unsigned_long_long_int = yes; then
	GDBM_COUNT_T="unsigned long long int"
else
	GDBM_COUNT_T="unsigned long"
fi

AC_MSG_CHECKING([for TLS qualifier])
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([],
                   [static __thread int x = 0;])],
  [gdbm_cv__thread=__thread],
  [gdbm_cv__thread=])
AC_MSG_RESULT($gdbm_cv__thread)
AC_DEFINE_UNQUOTED([GDBM_THREAD_LOCAL],$gdbm_cv__thread,[TLS qualifier])

dnl Internationalization macros.
AM_GNU_GETTEXT([external], [need-ngettext])
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19])
dnl The following kludge is needed when using autoconf 2.69.
dnl Observe the following:
dnl 1. Autopoint (0.19.8.1) understands the AM_GNU_GETTEXT_REQUIRE_VERSION
dnl    macro, but autoreconf 2.69 reports:
dnl      configure.ac: AM_GNU_GETTEXT is used, but not AM_GNU_GETTEXT_VERSION
dnl    and doesn't call autopoint at all.
dnl 2. On the other hand, when both macros are used, autopoint warns:
dnl      using AM_GNU_GETTEXT_REQUIRE_VERSION instead of AM_GNU_GETTEXT_VERSION
dnl    (which it actually does).
dnl
dnl To make both happy, the following kludge is installed, which uses the fact
dnl that autoreconf 2.69 looks for /^AM_GNU_GETTEXT_VERSION/ (see line 341),
dnl and autopoint, when working with the file written for autoconf 2.69, uses a
dnl simple sed logic to detect the presence of either macro.  The kludge below
dnl easily fools that logic.  When working with autoconf 2.70 and later,
dnl autopoint traces the m4 output, in which m4_if expands to nothing.
dnl
dnl When we finally switch to 2.70, this will be removed.
dnl
m4_if([
AM_GNU_GETTEXT_VERSION
])

AC_CHECK_HEADERS([sys/file.h string.h locale.h getopt.h])

AC_CHECK_FUNCS([ftruncate flock lockf fsync setlocale getopt_long])

if test x$mapped_io = xyes
then
  AC_FUNC_MMAP()
  AC_CHECK_FUNCS([msync])
fi
AC_TYPE_OFF_T
AC_CHECK_SIZEOF(off_t)
AC_CHECK_MEMBERS([struct stat.st_blksize, struct stat.st_mtim, struct stat.st_mtimespec],[],[],
                 [#include <sys/types.h>
	          #include <sys/stat.h>])

AM_CONDITIONAL([COMPAT_OPT], [test "$want_compat" = yes])

# Check for Curses libs.
for lib in ncurses curses termcap
do
  AC_CHECK_LIB($lib, tputs, [CURSES_LIBS="-l$lib"; break])
done

AC_SUBST(CURSES_LIBS)
# Readline
AC_ARG_WITH([readline],
            AS_HELP_STRING([--without-readline],
                           [do not use readline]),
            [
case "${withval}" in
  yes) status_readline=yes ;;
  no)  status_readline=no ;;
  *)   AC_MSG_ERROR(bad value ${withval} for --without-readline) ;;
esac],[status_readline=probe])

AC_SUBST(READLINE_LIBS)

if test "$status_readline" != "no"; then
  dnl FIXME This should only link in the curses libraries if it's
  dnl really needed!
  saved_LIBS=$LIBS
  LIBS="$LIBS $CURSES_LIBS"
  AC_CHECK_LIB(readline, readline,
    [status_readline=yes],
    [if test "$status_readline" = "yes"; then
       AC_MSG_ERROR(readline requested but does not seem to be installed)
     else
       status_readline=no
     fi])
  LIBS=$saved_LIBS

  if test "$status_readline" = "yes"; then
    AC_CHECK_HEADERS(readline/readline.h,
      AC_DEFINE(WITH_READLINE,1,[Enable use of readline]))
      READLINE_LIBS="-lreadline $CURSES_LIBS"
      saved_LIBS=$LIBS
      LIBS="$LIBS $READLINE_LIBS"
      AC_CHECK_FUNCS(rl_completion_matches)
      LIBS=$saved_LIBS
  fi
else
  status_readline=no
fi

AM_CONDITIONAL([GDBM_COND_READLINE], [test "$status_readline" = "yes"])

# CoW crash tolerance support
AC_ARG_ENABLE([crash-tolerance],
  AS_HELP_STRING(
    [--enable-crash-tolerance],
    [Enable crash tolerance, based on clone/copy-on-write (needs support for FICLONE)]),
  [status_ficlone=$enableval],
  [status_ficlone=probe])

if test $status_ficlone = probe; then
  AC_CHECK_HEADER([linux/fs.h],
    [AC_MSG_CHECKING([for FICLONE ioctl support])
       AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
         #include <linux/fs.h>
         #include <sys/ioctl.h>],
	 [ioctl(0, FICLONE, 1);])],
       [status_ficlone=yes],
       [status_ficlone=no])
     AC_MSG_RESULT($status_ficlone)],
     [status_ficlone=no])
fi
if test $status_ficlone = yes; then
  AC_DEFINE([GDBM_FAILURE_ATOMIC], 1, [Define if support for atomic failures is enabled])
fi

# Additional debugging
AC_ARG_ENABLE([debug],
              AS_HELP_STRING([--enable-debug],
                             [provide additional debugging information]),
              [status_debug=$withval],
	      [status_debug=no])

AC_SUBST([GDBM_DEBUG_ENABLE])
if test "$status_debug" = "yes"; then
  GDBM_DEBUG_ENABLE=1
else
  GDBM_DEBUG_ENABLE=0
fi  
AM_CONDITIONAL([GDBM_COND_DEBUG_ENABLE], [test "$status_debug" = "yes"])

AC_SUBST(YFLAGS_DEBUG)
AC_SUBST(LFLAGS_DEBUG)
if test "$want_gdbmtool_debug" = yes; then
    YFLAGS_DEBUG=-t
    LFLAGS_DEBUG=-d
fi    

AM_CONDITIONAL([COND_GDBMTOOL_DEBUG], [test "$want_gdbmtool_debug" = yes])

# Initialize the test suite.
AC_CONFIG_TESTDIR(tests)
AC_CONFIG_FILES([tests/Makefile tests/atlocal po/Makefile.in])
AC_CONFIG_FILES([tests/gdbmtool/Makefile])
AM_MISSING_PROG([AUTOM4TE], [autom4te])

AC_CONFIG_COMMANDS([status],[
cat <<EOF

*******************************************************************
GDBM settings summary:

Compatibility library ......................... $status_compat
Memory mapped I/O ............................. $mapped_io
GNU Readline .................................. $status_readline
Debugging support ............................. $status_debug
Reflink crash tolerance ....................... $status_ficlone
*******************************************************************

EOF
if test "$want_gdbmtool_debug" != default; then
    cat <<EOF    
NOTE: You used the --enable-gdbmtool-debug option. In order for your changes
NOTE: to take effect, please run the following command prior to building the
NOTE: package:
NOTE:
NOTE:   make -C src maintainer-clean-generic
NOTE:
EOF
fi
],
[status_compat=$want_compat
mapped_io=$mapped_io
status_readline=$status_readline
status_debug=$status_debug
want_gdbmtool_debug=$want_gdbmtool_debug
status_ficlone=$status_ficlone])

AC_CONFIG_FILES([Makefile
                 src/Makefile
		 src/gdbm.h
		 tools/Makefile
		 doc/Makefile
		 compat/Makefile])

AC_OUTPUT
