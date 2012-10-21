/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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
#include "libsigrok.h"
#include "libsigrok-internal.h"

/**
 * @defgroup grp_datastore Datastore
 *
 * Creating, using, or destroying libsigrok datastores.
 *
 * @{
 */

static gpointer new_chunk(struct sr_datastore **ds);

/**
 * Create a new datastore with the specified unit size.
 *
 * The unit size is fixed once the datastore is created, and cannot be
 * changed later on, neither can data be added to the datastore with
 * different unit sizes later.
 *
 * It is the caller's responsibility to free the allocated memory of the
 * datastore via the sr_datastore_destroy() function, if no longer needed.
 *
 * TODO: Unitsize should probably be unsigned int or uint32_t or similar.
 * TODO: This function should have a 'chunksize' parameter, and
 *       struct sr_datastore a 'chunksize' field.
 *
 * @param unitsize The unit size (>= 1) to be used for this datastore.
 * @param ds Pointer to a variable which will hold the newly created
 *           datastore structure.
 *           
 * @return SR_OK upon success, SR_ERR_MALLOC upon memory allocation errors,
 *         or SR_ERR_ARG upon invalid arguments. If something other than SR_OK
 *         is returned, the value of 'ds' is undefined.
 */
SR_API int sr_datastore_new(int unitsize, struct sr_datastore **ds)
{
	if (!ds) {
		sr_err("ds: %s: ds was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (unitsize <= 0) {
		sr_err("ds: %s: unitsize was %d, but it must be >= 1",
		       __func__, unitsize);
		return SR_ERR_ARG;
	}

	if (!(*ds = g_try_malloc(sizeof(struct sr_datastore)))) {
		sr_err("ds: %s: ds malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	(*ds)->ds_unitsize = unitsize;
	(*ds)->num_units = 0;
	(*ds)->chunklist = NULL;

	return SR_OK;
}

/**
 * Destroy the specified datastore and free the memory used by it.
 *
 * This will free the memory used by the data in the datastore's 'chunklist',
 * by the chunklist data structure itself, and by the datastore struct.
 *
 * @param ds The datastore to destroy.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 */
SR_API int sr_datastore_destroy(struct sr_datastore *ds)
{
	GSList *chunk;

	if (!ds) {
		sr_err("ds: %s: ds was NULL", __func__);
		return SR_ERR_ARG;
	}

	for (chunk = ds->chunklist; chunk; chunk = chunk->next)
		g_free(chunk->data);
	g_slist_free(ds->chunklist);
	g_free(ds);
	ds = NULL;

	return SR_OK;
}

/**
 * Append some data to the specified datastore.
 *
 * TODO: More elaborate function description.
 *
 * TODO: This function should use the (not yet available) 'chunksize' field
 *       of struct sr_datastore (instead of hardcoding DATASTORE_CHUNKSIZE).
 * TODO: in_unitsize and probelist are unused?
 * TODO: A few of the parameters can be const.
 * TODO: Ideally, 'ds' should be unmodified upon errors.
 *
 * @param ds Pointer to the datastore which shall receive the data.
 *           Must not be NULL.
 * @param data Pointer to the memory buffer containing the data to add.
 *             Must not be NULL. TODO: Data format?
 * @param length Length of the data to add (in number of bytes).
 *               TODO: Should 0 be allowed as length?
 * @param in_unitsize The unit size (>= 1) of the input data.
 * @param probelist Pointer to a list of integers (probe numbers). The probe
 *                  numbers in this list are 1-based, i.e. the first probe
 *                  is expected to be numbered 1 (not 0!). Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR_MALLOC upon memory allocation errors,
 *         or SR_ERR_ARG upon invalid arguments. If something other than SR_OK
 *         is returned, the value/state of 'ds' is undefined.
 */
SR_API int sr_datastore_put(struct sr_datastore *ds, void *data,
		unsigned int length, int in_unitsize, const int *probelist)
{
	unsigned int stored;
	int capacity, size, num_chunks, chunk_bytes_free, chunk_offset;
	gpointer chunk;

	if (!ds) {
		sr_err("ds: %s: ds was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* Unitsize must not be 0, we'll divide by 0 otherwise. */
	if (ds->ds_unitsize == 0) {
		sr_err("ds: %s: ds->ds_unitsize was 0", __func__);
		return SR_ERR_ARG;
	}

	if (!data) {
		sr_err("ds: %s: data was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (in_unitsize < 1) {
		sr_err("ds: %s: in_unitsize was %d, but it must be >= 1",
		       __func__, in_unitsize);
		return SR_ERR_ARG;
	}

	if (!probelist) {
		sr_err("ds: %s: probelist was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* Get the last chunk in the list, or create a new one if needed. */
	if (ds->chunklist == NULL) {
		if (!(chunk = new_chunk(&ds))) {
			sr_err("ds: %s: couldn't allocate new chunk", __func__);
			return SR_ERR_MALLOC;
		}
	} else {
		chunk = g_slist_last(ds->chunklist)->data;
	}

	/* Get/calculate number of chunks, free space, etc. */
	num_chunks = g_slist_length(ds->chunklist);
	capacity = (num_chunks * DATASTORE_CHUNKSIZE);
	chunk_bytes_free = capacity - (ds->ds_unitsize * ds->num_units);
	chunk_offset = capacity - (DATASTORE_CHUNKSIZE * (num_chunks - 1))
		       - chunk_bytes_free;

	stored = 0;
	while (stored < length) {
		/* No more free space left, allocate a new chunk. */
		if (chunk_bytes_free == 0) {
			if (!(chunk = new_chunk(&ds))) {
				sr_err("ds: %s: couldn't allocate new chunk",
				       __func__);
				return SR_ERR_MALLOC;
			}
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

	return SR_OK;
}

/**
 * Allocate a new memory chunk, append it to the datastore's chunklist.
 *
 * The newly allocated chunk is added to the datastore's chunklist by this
 * function, and the return value additionally points to the new chunk.
 *
 * The allocated memory is guaranteed to be cleared.
 *
 * TODO: This function should use the datastore's 'chunksize' field instead
 *       of hardcoding DATASTORE_CHUNKSIZE.
 * TODO: Return int, so we can return SR_OK / SR_ERR_ARG / SR_ERR_MALLOC?
 *
 * @param ds Pointer to a variable which holds the datastore structure.
 *           Must not be NULL. The contents of 'ds' are modified in-place.
 *
 * @return Pointer to the newly allocated chunk, or NULL upon failure.
 */
static gpointer new_chunk(struct sr_datastore **ds)
{
	gpointer chunk;

	/* Note: Caller checked that ds != NULL. */

	chunk = g_try_malloc0(DATASTORE_CHUNKSIZE * (*ds)->ds_unitsize);
	if (!chunk) {
		sr_err("ds: %s: chunk malloc failed (ds_unitsize was %u)",
		       __func__, (*ds)->ds_unitsize);
		return NULL; /* TODO: SR_ERR_MALLOC later? */
	}

	(*ds)->chunklist = g_slist_append((*ds)->chunklist, chunk);

	return chunk; /* TODO: SR_OK later? */
}

/** @} */
