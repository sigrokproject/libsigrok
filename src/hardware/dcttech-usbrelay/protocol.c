/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include <config.h>

#include <string.h>

#include "protocol.h"

SR_PRIV int dcttech_usbrelay_update_state(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t report[1 + REPORT_BYTECOUNT];
	int ret;
	GString *txt;

	devc = sdi->priv;

	/* Get another HID report. */
	memset(report, 0, sizeof(report));
	report[0] = REPORT_NUMBER;
	ret = hid_get_feature_report(devc->hid_dev, report, sizeof(report));
	if (ret != sizeof(report))
		return SR_ERR_IO;
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		txt = sr_hexdump_new(report, sizeof(report));
		sr_spew("Got report bytes: %s.", txt->str);
		sr_hexdump_free(txt);
	}

	/* Update relay state cache from HID report content. */
	devc->relay_state = report[1 + STATE_INDEX];
	devc->relay_state &= devc->relay_mask;

	return SR_OK;
}

SR_PRIV int dcttech_usbrelay_switch_cg(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean on)
{
	struct dev_context *devc;
	struct channel_group_context *cgc;
	gboolean is_all;
	size_t relay_idx;
	uint8_t report[1 + REPORT_BYTECOUNT];
	int ret;
	GString *txt;

	devc = sdi->priv;

	/* Determine if all or a single relay should be turned off or on. */
	is_all = !cg ? TRUE : FALSE;
	if (is_all) {
		relay_idx = 0;
	} else {
		cgc = cg->priv;
		relay_idx = cgc->number;
	}

	/*
	 * Construct and send the HID report. Notice the weird(?) bit
	 * pattern. Bit 1 is low when all relays are affected at once,
	 * and high to control an individual relay? Bit 0 communicates
	 * whether the relay(s) should be on or off? And all other bits
	 * are always set? It's assumed that the explicit assignment of
	 * full byte values simplifies future maintenance.
	 */
	memset(report, 0, sizeof(report));
	report[0] = REPORT_NUMBER;
	if (is_all) {
		if (on) {
			report[1] = 0xfe;
		} else {
			report[1] = 0xfc;
		}
	} else {
		if (on) {
			report[1] = 0xff;
			report[2] = relay_idx;
		} else {
			report[1] = 0xfd;
			report[2] = relay_idx;
		}
	}
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		txt = sr_hexdump_new(report, sizeof(report));
		sr_spew("Sending report bytes: %s", txt->str);
		sr_hexdump_free(txt);
	}
	ret = hid_send_feature_report(devc->hid_dev, report, sizeof(report));
	if (ret != sizeof(report))
		return SR_ERR_IO;

	/* Update relay state cache (non-fatal). */
	(void)dcttech_usbrelay_update_state(sdi);

	return SR_OK;
}

/* Answers the query from cached relay state. Beware of 1-based indexing. */
SR_PRIV int dcttech_usbrelay_query_cg(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean *on)
{
	struct dev_context *devc;
	struct channel_group_context *cgc;
	size_t relay_idx;
	uint32_t relay_mask;

	devc = sdi->priv;
	if (!cg)
		return SR_ERR_ARG;
	cgc = cg->priv;
	relay_idx = cgc->number;
	if (relay_idx < 1 || relay_idx > devc->relay_count)
		return SR_ERR_ARG;
	relay_mask = 1U << (relay_idx - 1);

	*on = devc->relay_state & relay_mask;

	return SR_OK;
}
