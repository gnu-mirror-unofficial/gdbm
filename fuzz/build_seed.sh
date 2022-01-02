#!/bin/sh
# This file is part of GDBM.
# Copyright (C) 2021-2022 Free Software Foundation, Inc.
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
# along with GDBM. If not, see <http://www.gnu.org/licenses/>. */

set -e

unset wd
while getopts "C:" OPTION
do
    case $OPTION in
	C) wd=$OPTARG;;
    esac
done

if [ -n "$wd" ]; then
    if [ ! -d $wd ]; then
	mkdir $wd
    fi
    cd $wd
fi

for format in standard numsync; do
   gdbmtool <<EOF
set format=$format
open empty_$format
close

open one_$format
store key1 value1
close

set blocksize=512
open empty_b512_$format
close

set cachesize=512
open empty_b512_c512_$format
close

open one_b512_c512_$format
store key1 value1
close

open ten_b512_c512_$format
store key1 value1
store key2 value2
store key3 value3
store key4 value4
store key5 value5
store key6 value6
store key7 value7
store key8 value8
store key9 value9
store key10 value10

open nine_b512_c512_$format
store key1 value1
store key2 value2
store key3 value3
store key4 value4
store key5 value5
store key6 value6
store key7 value7
store key8 value8
store key9 value9
store key10 value10
delete key1
close

open one_b512_c512_ku_cs_$format
define key { uint k }
define content { string s }
store 1 value1
close

open one_b512_c512_ku_cu_$format
define key { uint k }
define content { uint v }
store 1 1
define key { string k }
store key1 1
define key { uint k }
define content { uint v[2] }
store 1 { { 1 , 2 } }
list
close

open one_b512_c512_ku_cusz_$format
define key { uint k }
define content { uint v, stringz s }
store 1 { 1 , value1 }
list
close
quit
EOF
done
