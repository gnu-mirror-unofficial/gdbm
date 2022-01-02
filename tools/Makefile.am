# This file is part of GDBM.                                   -*- Makefile -*-
# Copyright (C) 2007-2022 Free Software Foundation, Inc.
#
# GDBM is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.
#
# GDBM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GDBM. If not, see <http://www.gnu.org/licenses/>. 

# Flags
AM_CPPFLAGS=\
 -DLOCALEDIR=\"$(localedir)\"\
 -I$(top_srcdir)/src\
 -I$(top_builddir)/src\
 -I$(top_srcdir)/tools

noinst_LIBRARIES = libgdbmapp.a

libgdbmapp_a_SOURCES =\
 err.c\
 mem.c\
 gdbmapp.h\
 parseopt.c\
 progname.c\
 datconv.c\
 gram.c\
 input-argv.c\
 input-file.c\
 input-null.c\
 input-std.c\
 lex.c\
 gdbmshell.c\
 var.c\
 util.c\
 wordwrap.c

if GDBM_COND_READLINE
  libgdbmapp_a_SOURCES += input-rl.c
endif

# Programs
bin_PROGRAMS = gdbmtool gdbm_load gdbm_dump

EXTRA_DIST = gram.y lex.l
BUILT_SOURCES = gram.h gram.c lex.c
noinst_HEADERS =  gram.h

gram.c gram.h: gram.y
lex.c: lex.l

gdbmtool_LDADD  = \
 ./libgdbmapp.a\
 ../src/libgdbm.la\
 @READLINE_LIBS@

gdbmtool_SOURCES = \
 gdbmtool.h\
 gdbmtool.c

AM_YFLAGS = -dv $(YFLAGS_DEBUG)
AM_LFLAGS = $(LFLAGS_DEBUG)

.l.c:
	$(AM_V_GEN)$(FLEX) -o $@ $(AM_LFLAGS) $<
.y.c:
	$(AM_V_GEN)$(BISON) -o $@ $(AM_YFLAGS) $<

if COND_GDBMTOOL_DEBUG
  AM_CPPFLAGS += -DGDBMTOOL_DEBUG=1
endif

gdbm_load_LDADD  = ./libgdbmapp.a ../src/libgdbm.la 
gdbm_dump_LDADD  = ./libgdbmapp.a ../src/libgdbm.la 
