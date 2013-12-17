/*
 * This file is part of the libsigrok project.
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
typedef int gboolean;

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

typedef guint (*GHashFunc)(gconstpointer key);
typedef gboolean (*GEqualFunc)(gconstpointer a, gconstpointer b);
typedef void (*GDestroyNotify)(gpointer data);

GHashTable *g_hash_table_new_full(GHashFunc hash_func, GEqualFunc key_equal_func,
        GDestroyNotify key_destroy_func, GDestroyNotify value_destroy_func);
void g_hash_table_insert(GHashTable *hash_table, gpointer key, gpointer value);
void g_hash_table_destroy(GHashTable *hash_table);

%callback("%s_ptr");
guint g_str_hash(gconstpointer v);
gboolean g_str_equal(gconstpointer v1, gconstpointer v2);;
void g_free(gpointer mem);
%nocallback;

gchar *g_strdup(const char *str);

typedef struct _GString GString;

struct _GString
{
  char *str;
  gsize len;
  gsize allocated_len;
};

gchar *g_string_free(GString *string, gboolean free_segment);

%include "libsigrok/libsigrok.h"
#undef SR_API
#define SR_API
%ignore sr_config_info_name_get;
%include "libsigrok/proto.h"
%include "libsigrok/version.h"

%array_class(float, float_array);
%pointer_functions(uint8_t *, uint8_ptr_ptr);
%pointer_functions(uint64_t, uint64_ptr);
%pointer_functions(GString *, gstring_ptr_ptr);
%pointer_functions(GVariant *, gvariant_ptr_ptr);
%array_functions(GVariant *, gvariant_ptr_array);
%pointer_functions(struct sr_context *, sr_context_ptr_ptr);
%array_functions(struct sr_dev_driver *, sr_dev_driver_ptr_array);
%array_functions(struct sr_input_format *, sr_input_format_ptr_array);
%array_functions(struct sr_output_format *, sr_output_format_ptr_array);
%pointer_cast(gpointer, struct sr_dev_inst *, gpointer_to_sr_dev_inst_ptr);
%pointer_cast(void *, struct sr_datafeed_logic *, void_ptr_to_sr_datafeed_logic_ptr)
%pointer_cast(void *, struct sr_datafeed_analog *, void_ptr_to_sr_datafeed_analog_ptr)
%pointer_cast(void *, struct sr_probe *, void_ptr_to_sr_probe_ptr)
%pointer_cast(void *, struct sr_probe_group *, void_ptr_to_sr_probe_group_ptr)

%extend sr_input_format {
        int call_format_match(const char *filename) {
                return $self->format_match(filename);
        }

        int call_init(struct sr_input *in, const char *filename) {
                return $self->init(in, filename);
        }

        int call_loadfile(struct sr_input *in, const char *filename) {
                return $self->loadfile(in, filename);
        }
}

%extend sr_output_format {
        int call_init(struct sr_output *o) {
                return $self->init(o);
        }

        int call_event(struct sr_output *o, int event_type, uint8_t **data_out,
                        uint64_t *length_out) {
                return $self->event(o, event_type, data_out, length_out);
        }

        int call_data(struct sr_output *o, const void *data_in,
                        uint64_t length_in, uint8_t **data_out, uint64_t *length_out) {
                return $self->data(o, data_in, length_in, data_out, length_out);
        }

        int call_receive(struct sr_output *o, const struct sr_dev_inst *sdi,
                        const struct sr_datafeed_packet *packet, GString **out) {
                return $self->receive(o, sdi, packet, out);
        }

        int call_cleanup(struct sr_output *o) {
                return $self->cleanup(o);
        }
}
