/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2013 Martin Ling <martin-sigrok@earth.li>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

%include "cpointer.i"
%include "carrays.i"
%include "stdint.i"

%{
#include "libsigrok/libsigrok.h"
%}

typedef void *gpointer;

typedef struct _GSList GSList;

struct _GSList
{
        gpointer data;
        GSList *next;
};

void g_slist_free(GSList *list);

GVariant *g_variant_new_uint64(uint64_t value);
GVariant *g_variant_new_boolean(gboolean value);
GVariant *g_variant_new_double(double value);
GVariant *g_variant_new_string(char *value);
GVariant *g_variant_new_tuple(GVariant *children[], unsigned long n_children);
char *g_variant_get_type_string(GVariant *value);
uint64_t g_variant_get_uint64(GVariant *value);
gboolean g_variant_get_boolean(GVariant *value);
double g_variant_get_double(GVariant *value);
char *g_variant_get_string(GVariant *value, unsigned long *length);
GVariant *g_variant_get_child_value(GVariant *value, unsigned long index);

%include "libsigrok/libsigrok.h"
#undef SR_API
#define SR_API
%ignore sr_config_info_name_get;
%include "libsigrok/proto.h"
%include "libsigrok/version.h"

%pointer_functions(GVariant *, gvariant_ptr_ptr);
%array_functions(GVariant *, gvariant_ptr_array);
%pointer_functions(struct sr_context *, sr_context_ptr_ptr);
%array_functions(struct sr_dev_driver *, sr_dev_driver_ptr_array);
%pointer_cast(gpointer, struct sr_dev_inst *, gpointer_to_sr_dev_inst_ptr);
%pointer_cast(void *, struct sr_datafeed_logic *, void_ptr_to_sr_datafeed_logic_ptr)
