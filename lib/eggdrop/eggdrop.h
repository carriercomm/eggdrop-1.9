/*
 * eggdrop.h
 *   libeggdrop header file
 *
 * $Id: eggdrop.h,v 1.5 2002/03/22 16:01:16 ite Exp $
 */
/*
 * Copyright (C) 1999, 2000, 2001, 2002 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _EGGDROP_H
#define _EGGDROP_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "../compat/compat.h"
#include "../egglib/egglib.h"
#include <eggdrop/common.h>
#include <eggdrop/botnetutil.h>
#include <eggdrop/memutil.h>
#include <eggdrop/fileutil.h>
#include <eggdrop/registry.h>

BEGIN_C_DECLS

typedef struct eggdrop {
  Function *global;		/* FIXME: this field will be removed once the
				   global_funcs mess is cleaned up */
  hash_table_t *registry_table;
} eggdrop_t;

extern eggdrop_t *eggdrop_new(void);
extern eggdrop_t *eggdrop_delete(eggdrop_t *);

END_C_DECLS

#endif				/* _EGGDROP_H */
