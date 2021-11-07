# This file is part of GDBM.                                   -*- Makefile -*-
# Copyright (C) 2007-2021 Free Software Foundation, Inc.
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
AM_CPPFLAGS=-DIN_GDBM -DLOCALEDIR=\"$(localedir)\"

# Headers
nodist_include_HEADERS = gdbm.h
noinst_HEADERS = \
 gdbmconst.h\
 gdbmdefs.h\
 gettext.h\
 gram.h\
 proto.h\
 systems.h

EXTRA_DIST = gdbm.h.in gdbm.magic gram.y lex.l
BUILT_SOURCES = gdbm.h gram.h gram.c lex.c

gram.c gram.h: gram.y
lex.c: lex.l

# The libraries
VI_CURRENT  = 6
VI_REVISION = 0
VI_AGE      = 0

lib_LTLIBRARIES = libgdbm.la
libgdbm_la_LIBADD = @LTLIBINTL@

libgdbm_la_SOURCES = \
 gdbmclose.c\
 gdbmcount.c\
 gdbmdelete.c\
 gdbmdump.c\
 gdbmerrno.c\
 gdbmexists.c\
 gdbmexp.c\
 gdbmfdesc.c\
 gdbmfetch.c\
 gdbmload.c\
 gdbmopen.c\
 gdbmimp.c\
 gdbmreorg.c\
 gdbmseq.c\
 gdbmsetopt.c\
 gdbmstore.c\
 gdbmsync.c\
 avail.c\
 base64.c\
 bucket.c\
 falloc.c\
 findkey.c\
 fullio.c\
 hash.c\
 lock.c\
 mmap.c\
 recover.c\
 update.c\
 version.c

if GDBM_COND_DEBUG_ENABLE
  libgdbm_la_SOURCES += debug.c
endif

libgdbm_la_LDFLAGS = -version-info $(VI_CURRENT):$(VI_REVISION):$(VI_AGE)

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
 util.c

if GDBM_COND_READLINE
  libgdbmapp_a_SOURCES += input-rl.c
endif

# Programs
bin_PROGRAMS = gdbmtool gdbm_load gdbm_dump

gdbmtool_LDADD  = \
 ./libgdbmapp.a\
 ./libgdbm.la\
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

gdbm_load_LDADD  = ./libgdbmapp.a ./libgdbm.la 
gdbm_dump_LDADD  = ./libgdbmapp.a ./libgdbm.la 
