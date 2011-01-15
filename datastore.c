/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <sigrok.h>

static gpointer new_chunk(struct datastore **ds);

int datastore_new(int unitsize, struct datastore **ds)
{
	if (!ds)
		return SIGROK_ERR;

	if (unitsize <= 0)
		return SIGROK_ERR; /* TODO: Different error? */

	if (!(*ds = g_malloc(sizeof(struct datastore))))
		return SIGROK_ERR_MALLOC;

	(*ds)->ds_unitsize = unitsize;
	(*ds)->num_units = 0;
	(*ds)->chunklist = NULL;

	return SIGROK_OK;
}

int datastore_destroy(struct datastore *ds)
{
	GSList *chunk;

	if (!ds)
		return SIGROK_ERR;

	for (chunk = ds->chunklist; chunk; chunk = chunk->next)
		g_free(chunk->data);
	g_slist_free(ds->chunklist);
	g_free(ds);

	return SIGROK_OK;
}

void datastore_put(struct datastore *ds, void *data, unsigned int length,
		   int in_unitsize, int *probelist)
{
	unsigned int stored;
	int capacity, size, num_chunks, chunk_bytes_free, chunk_offset;
	gpointer chunk;

	/* Avoid compiler warnings. */
	in_unitsize = in_unitsize;
	probelist = probelist;

	if (ds->chunklist == NULL)
		chunk = new_chunk(&ds);
	else
		chunk = g_slist_last(ds->chunklist)->data;

	num_chunks = g_slist_length(ds->chunklist);
	capacity = (num_chunks * DATASTORE_CHUNKSIZE);
	chunk_bytes_free = capacity - (ds->ds_unitsize * ds->num_units);
	chunk_offset = capacity - (DATASTORE_CHUNKSIZE * (num_chunks - 1))
		       - chunk_bytes_free;
	stored = 0;
	while (stored < length) {
		if (chunk_bytes_free == 0) {
			chunk = new_chunk(&ds);
			chunk_bytes_free = DATASTORE_CHUNKSIZE;
			chunk_offset = 0;
		}

		if (length - stored > (unsigned int)chunk_bytes_free)
			size = chunk_bytes_free;
		else
			/* Last part, won't fill up this chunk. */
			size = length - stored;

		memcpy(chunk + chunk_offset, data + stored, size);
		chunk_bytes_free -= size;
		stored += size;
	}
	ds->num_units += stored / ds->ds_unitsize;
}

static gpointer new_chunk(struct datastore **ds)
{
	gpointer chunk;

	if (!(chunk = malloc(DATASTORE_CHUNKSIZE * (*ds)->ds_unitsize)))
		return NULL;

	(*ds)->chunklist = g_slist_append((*ds)->chunklist, chunk);

	return chunk;
}
