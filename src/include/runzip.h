/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
   Copyright (C) 2006-2011 Con Kolivas
   Copyright (C) 2011, 2020, 2022 Peter Hyman
   Copyright (C) 1998-2003 Andrew Tridgell
*/

#ifndef RUNZIP_H
#define RUNZIP_H

#include "lrzip_private.h"

void clear_rulist(rzip_control *control);
i64 runzip_fd(rzip_control *control, int fd_in, int fd_out, int fd_hist, i64 expected_size);

#endif
