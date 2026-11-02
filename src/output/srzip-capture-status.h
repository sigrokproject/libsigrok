/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Preserve terminal META arriving after the first logic chunk. */
static int zip_update_capture_status(const struct sr_output *o)
{
	struct out_context *outc = o->priv;
	struct zip *archive;
	struct zip_stat metadata;
	struct zip_source *source;
	GKeyFile *keyfile;
	char *text;
	gsize length;

	if (!outc->capture_status && !outc->capture_error)
		return SR_OK;
	archive = zip_open(outc->filename, 0, NULL);
	if (!archive)
		return SR_ERR;
	if (zip_stat(archive, "metadata", 0, &metadata) < 0) {
		zip_discard(archive);
		return SR_ERR;
	}
	keyfile = sr_sessionfile_read_metadata(archive, &metadata);
	if (!keyfile) {
		zip_discard(archive);
		return SR_ERR_DATA;
	}
	if (outc->capture_status)
		g_key_file_set_string(keyfile, "device 1", "capture status", outc->capture_status);
	if (outc->capture_error)
		g_key_file_set_string(keyfile, "device 1", "capture error", outc->capture_error);
	text = g_key_file_to_data(keyfile, &length, NULL);
	g_key_file_free(keyfile);
	source = zip_source_buffer(archive, text, length, TRUE);
	if (!source) {
		g_free(text);
		zip_discard(archive);
		return SR_ERR;
	}
	if (zip_file_replace(archive, metadata.index, source, 0) < 0) {
		zip_source_free(source);
		zip_discard(archive);
		return SR_ERR;
	}
	if (zip_close(archive) < 0) {
		zip_discard(archive);
		return SR_ERR;
	}
	return SR_OK;
}
