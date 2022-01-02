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

# Headers
nodist_include_HEADERS = gdbm.h
noinst_HEADERS = \
 gdbmconst.h\
 gdbmdefs.h\
 gettext.h\
 proto.h\
 systems.h

EXTRA_DIST = gdbm.h.in gdbm.magic
BUILT_SOURCES = gdbm.h 

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

