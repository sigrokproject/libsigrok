/* SPDX-License-Identifier: GPL-3.0-only */
/* Host outcomes only. No USB commands, filtering, retry or timing changes. */
static void capture_set_failure(struct dev_context *devc,
	const char *status, const char *reason)
{
	/* An explicit loss is more informative than a later timeout/cleanup error. */
	if (!g_strcmp0(devc->capture_status, "disconnected"))
		return;
	devc->capture_status = status;
	devc->capture_error = reason;
}

static void capture_finish_status(struct dev_context *devc)
{
#if defined(_WIN32) && DLA_WCH_WORKER
	if (devc->wch_disconnected) {
		devc->capture_status = "disconnected";
		devc->capture_error = "Capture incomplete: DLA-32 disconnected. Reconnect USB, then use Connect to Device, Scan, and select the analyzer again.";
		return;
	}
#endif
#ifdef _WIN32
	if (devc->use_wch && devc->upload_stop_pending &&
			g_strcmp0(devc->capture_status, "disconnected")) {
		capture_set_failure(devc, "error", "Acquisition ended, but the analyzer did not acknowledge Stop. Close and reopen the device before retrying.");
		return;
	}
#endif
	if (devc->capture_status && strcmp(devc->capture_status, "running"))
		return;
	/* 'complete' is a delivered-count result, never proof of physical integrity. */
	devc->capture_status = devc->run_limit_samples &&
		devc->limits.samples_read >= devc->run_limit_samples ? "complete" : "stopped";
	devc->capture_error = "";
}

static int capture_send_status(const struct sr_dev_inst *sdi)
{
	const struct dev_context *devc = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config status = {SR_CONF_CAPTURE_STATUS,
		g_variant_ref_sink(g_variant_new_string(devc->capture_status ? devc->capture_status : "unknown"))};
	struct sr_config reason = {SR_CONF_CAPTURE_ERROR,
		g_variant_ref_sink(g_variant_new_string(devc->capture_error ? devc->capture_error : ""))};
	int ret;

	meta.config = g_slist_append(NULL, &status);
	meta.config = g_slist_append(meta.config, &reason);
	packet.type = SR_DF_META;
	packet.payload = &meta;
	ret = sr_session_send(sdi, &packet);
	g_slist_free(meta.config);
	g_variant_unref(status.data);
	g_variant_unref(reason.data);
	return ret;
}
