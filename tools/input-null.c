/* This file is part of GDBM, the GNU data base manager.
   Copyright (C) 2018-2022 Free Software Foundation, Inc.

   GDBM is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GDBM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GDBM. If not, see <http://www.gnu.org/licenses/>.    */

#include "gdbmtool.h"

static ssize_t
instream_null_read (instream_t istr, char *buf, size_t size)
{
  return 0;
}

static void
instream_null_close (instream_t istr)
{
  free (istr);
}

static int
instream_null_eq (instream_t a, instream_t b)
{
  return a == b;
}

instream_t
instream_null_create (void)
{
  struct instream *istr;

  istr = emalloc (sizeof *istr);
  istr->in_name = "null";
  istr->in_inter = 0;
  istr->in_read = instream_null_read;
  istr->in_close = instream_null_close;
  istr->in_eq = instream_null_eq;
  istr->in_history_size = NULL;
  istr->in_history_get = NULL;  

  return istr;
}

