/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Martin Ling <martin-git@earth.li>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2013 Mathias Grimmberger <mgri@zaphod.sax.de>
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
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"
#include "protocol.h"

/*
 * This is a unified protocol driver for the DS1000 and DS2000 series.
 *
 * DS1000 support tested with a Rigol DS1102D.
 *
 * DS2000 support tested with a Rigol DS2072 using firmware version 01.01.00.02.
 *
 * The Rigol DS2000 series scopes try to adhere to the IEEE 488.2 (I think)
 * standard. If you want to read it - it costs real money...
 *
 * Every response from the scope has a linefeed appended because the
 * standard says so. In principle this could be ignored because sending the
 * next command clears the output queue of the scope. This driver tries to
 * avoid doing that because it may cause an error being generated inside the
 * scope and who knows what bugs the firmware has WRT this.
 *
 * Waveform data is transferred in a format called "arbitrary block program
 * data" specified in IEEE 488.2. See Agilents programming manuals for their
 * 2000/3000 series scopes for a nice description.
 *
 * Each data block from the scope has a header, e.g. "#900000001400".
 * The '#' marks the start of a block.
 * Next is one ASCII decimal digit between 1 and 9, this gives the number of
 * ASCII decimal digits following.
 * Last are the ASCII decimal digits giving the number of bytes (not
 * samples!) in the block.
 *
 * After this header as many data bytes as indicated follow.
 *
 * Each data block has a trailing linefeed too.
 */

static int parse_int(const char *str, int *ret)
{
	char *e;
	long tmp;

	errno = 0;
	tmp = strtol(str, &e, 10);
	if (e == str || *e != '\0') {
		sr_dbg("Failed to parse integer: '%s'", str);
		return SR_ERR;
	}
	if (errno) {
		sr_dbg("Failed to parse integer: '%s', numerical overflow", str);
		return SR_ERR;
	}
	if (tmp > INT_MAX || tmp < INT_MIN) {
		sr_dbg("Failed to parse integer: '%s', value to large/small", str);
		return SR_ERR;
	}

	*ret = (int)tmp;
	return SR_OK;
}

/* Set the next event to wait for in rigol_ds_receive */
static void rigol_ds_set_wait_event(struct dev_context *devc, enum wait_events event)
{
	if (event == WAIT_STOP)
		devc->wait_status = 2;
	else
		devc->wait_status = 1;
	devc->wait_event = event;
}

/*
 * Waiting for a event will return a timeout after 2 to 3 seconds in order
 * to not block the application.
 */
static int rigol_ds_event_wait(const struct sr_dev_inst *sdi, char status1, char status2)
{
	char *buf, c;
	struct dev_context *devc;
	time_t start;

	if (!(devc = sdi->priv))
		return SR_ERR;

	start = time(NULL);

	/*
	 * Trigger status may return:
	 * "TD" or "T'D" - triggered
	 * "AUTO"        - autotriggered
	 * "RUN"         - running
	 * "WAIT"        - waiting for trigger
	 * "STOP"        - stopped
	 */

	if (devc->wait_status == 1) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger");
				return SR_ERR_TIMEOUT;
			}

			if (sr_scpi_get_string(sdi->conn, ":TRIG:STAT?", &buf) != SR_OK)
				return SR_ERR;
			c = buf[0];
			g_free(buf);
		} while (c == status1 || c == status2);

		devc->wait_status = 2;
	}
	if (devc->wait_status == 2) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger");
				return SR_ERR_TIMEOUT;
			}

			if (sr_scpi_get_string(sdi->conn, ":TRIG:STAT?", &buf) != SR_OK)
				return SR_ERR;
			c = buf[0];
			g_free(buf);
		} while (c != status1 && c != status2);

		rigol_ds_set_wait_event(devc, WAIT_NONE);
	}

	return SR_OK;
}

/*
 * For live capture we need to wait for a new trigger event to ensure that
 * sample data is not returned twice.
 *
 * Unfortunately this will never really work because for sufficiently fast
 * timebases and trigger rates it just can't catch the status changes.
 *
 * What would be needed is a trigger event register with autoreset like the
 * Agilents have. The Rigols don't seem to have anything like this.
 *
 * The workaround is to only wait for the trigger when the timebase is slow
 * enough. Of course this means that for faster timebases sample data can be
 * returned multiple times, this effect is mitigated somewhat by sleeping
 * for about one sweep time in that case.
 */
static int rigol_ds_trigger_wait(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	long s;

	if (!(devc = sdi->priv))
		return SR_ERR;

	/*
	 * If timebase < 50 msecs/DIV just sleep about one sweep time except
	 * for really fast sweeps.
	 */
	if (devc->timebase < 0.0499) {
		if (devc->timebase > 0.99e-6) {
			/*
			 * Timebase * num hor. divs * 85(%) * 1e6(usecs) / 100
			 * -> 85 percent of sweep time
			 */
			s = (devc->timebase * devc->model->series->num_horizontal_divs
			     * 85e6) / 100L;
			sr_spew("Sleeping for %ld usecs instead of trigger-wait", s);
			g_usleep(s);
		}
		rigol_ds_set_wait_event(devc, WAIT_NONE);
		return SR_OK;
	} else {
		return rigol_ds_event_wait(sdi, 'T', 'A');
	}
}

/* Wait for scope to got to "Stop" in single shot mode */
static int rigol_ds_stop_wait(const struct sr_dev_inst *sdi)
{
	return rigol_ds_event_wait(sdi, 'S', 'S');
}

/* Check that a single shot acquisition actually succeeded on the DS2000 */
static int rigol_ds_check_stop(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	int tmp;

	if (!(devc = sdi->priv))
		return SR_ERR;

	ch = devc->channel_entry->data;

	if (devc->model->series->protocol != PROTOCOL_V3)
		return SR_OK;

	if (ch->type == SR_CHANNEL_LOGIC) {
		if (rigol_ds_config_set(sdi, ":WAV:SOUR LA") != SR_OK)
			return SR_ERR;
	} else {
		if (rigol_ds_config_set(sdi, ":WAV:SOUR CHAN%d",
				ch->index + 1) != SR_OK)
			return SR_ERR;
	}
	/* Check that the number of samples will be accepted */
	if (rigol_ds_config_set(sdi, ":WAV:POIN %d",
			ch->type == SR_CHANNEL_LOGIC ?
				devc->digital_frame_size :
				devc->analog_frame_size) != SR_OK)
		return SR_ERR;
	if (sr_scpi_get_int(sdi->conn, "*ESR?", &tmp) != SR_OK)
		return SR_ERR;
	/*
	 * If we get an "Execution error" the scope went from "Single" to
	 * "Stop" without actually triggering. There is no waveform
	 * displayed and trying to download one will fail - the scope thinks
	 * it has 1400 samples (like display memory) and the driver thinks
	 * it has a different number of samples.
	 *
	 * In that case just try to capture something again. Might still
	 * fail in interesting ways.
	 *
	 * Ain't firmware fun?
	 */
	if (tmp & 0x10) {
		sr_warn("Single shot acquisition failed, retrying...");
		/* Sleep a bit, otherwise the single shot will often fail */
		g_usleep(500 * 1000);
		rigol_ds_config_set(sdi, ":SING");
		rigol_ds_set_wait_event(devc, WAIT_STOP);
		return SR_ERR;
	}

	return SR_OK;
}

/* Wait for enough data becoming available in scope output buffer */
static int rigol_ds_block_wait(const struct sr_dev_inst *sdi)
{
	char *buf, c;
	struct dev_context *devc;
	time_t start;
	int len, ret;

	if (!(devc = sdi->priv))
		return SR_ERR;

	if (devc->model->series->protocol == PROTOCOL_V3) {

		start = time(NULL);

		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for data block");
				return SR_ERR_TIMEOUT;
			}

			/*
			 * The scope copies data really slowly from sample
			 * memory to its output buffer, so try not to bother
			 * it too much with SCPI requests but don't wait too
			 * long for short sample frame sizes.
			 */
			g_usleep(devc->analog_frame_size < (15 * 1000) ? (100 * 1000) : (1000 * 1000));

			/* "READ,nnnn" (still working) or "IDLE,nnnn" (finished) */
			if (sr_scpi_get_string(sdi->conn, ":WAV:STAT?", &buf) != SR_OK)
				return SR_ERR;
			ret = parse_int(buf + 5, &len);
			c = buf[0];
			g_free(buf);
			if (ret != SR_OK)
				return SR_ERR;
		} while (c == 'R' && len < (1000 * 1000));
	}

	rigol_ds_set_wait_event(devc, WAIT_NONE);

	return SR_OK;
}

/* Send a configuration setting. */
SR_PRIV int rigol_ds_config_set(const struct sr_dev_inst *sdi, const char *format, ...)
{
	struct dev_context *devc = sdi->priv;
	va_list args;
	int ret;

	va_start(args, format);
	ret = sr_scpi_send_variadic(sdi->conn, format, args);
	va_end(args);

	if (ret != SR_OK)
		return SR_ERR;

	if (devc->model->series->protocol == PROTOCOL_V2) {
		/* The DS1000 series needs this stupid delay, *OPC? doesn't work. */
		sr_spew("delay %dms", 100);
		g_usleep(100 * 1000);
		return SR_OK;
	} else {
		return sr_scpi_get_opc(sdi->conn);
	}
}

/* Start capturing a new frameset */
SR_PRIV int rigol_ds_capture_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	gchar *trig_mode;
	unsigned int num_channels, i, j;
	int buffer_samples;
	int ret;

	if (!(devc = sdi->priv))
		return SR_ERR;

	const gboolean first_frame = (devc->num_frames == 0);

	uint64_t limit_frames = devc->limit_frames;
	if (devc->num_frames_segmented != 0 && devc->num_frames_segmented < limit_frames)
		limit_frames = devc->num_frames_segmented;
	if (limit_frames == 0)
		sr_dbg("Starting data capture for frameset %" PRIu64,
		       devc->num_frames + 1);
	else
		sr_dbg("Starting data capture for frameset %" PRIu64 " of %"
		       PRIu64, devc->num_frames + 1, limit_frames);

	switch (devc->model->series->protocol) {
	case PROTOCOL_V1:
		rigol_ds_set_wait_event(devc, WAIT_TRIGGER);
		break;
	case PROTOCOL_V2:
		if (devc->data_source == DATA_SOURCE_LIVE) {
			if (rigol_ds_config_set(sdi, ":WAV:POIN:MODE NORMAL") != SR_OK)
				return SR_ERR;
			rigol_ds_set_wait_event(devc, WAIT_TRIGGER);
		} else {
			if (rigol_ds_config_set(sdi, ":STOP") != SR_OK)
				return SR_ERR;
			if (rigol_ds_config_set(sdi, ":WAV:POIN:MODE RAW") != SR_OK)
				return SR_ERR;
			if (sr_scpi_get_string(sdi->conn, ":TRIG:MODE?", &trig_mode) != SR_OK)
				return SR_ERR;
			ret = rigol_ds_config_set(sdi, ":TRIG:%s:SWE SING", trig_mode);
			g_free(trig_mode);
			if (ret != SR_OK)
				return SR_ERR;
			if (rigol_ds_config_set(sdi, ":RUN") != SR_OK)
				return SR_ERR;
			rigol_ds_set_wait_event(devc, WAIT_STOP);
		}
		break;
	case PROTOCOL_V3:
	case PROTOCOL_V4:
	case PROTOCOL_V5:
	case PROTOCOL_V6:
		if (first_frame && rigol_ds_config_set(sdi, ":WAV:FORM BYTE") != SR_OK)
			return SR_ERR;
		if (devc->data_source == DATA_SOURCE_LIVE) {
			if (first_frame && rigol_ds_config_set(sdi, ":WAV:MODE NORM") != SR_OK)
				return SR_ERR;
			devc->analog_frame_size = devc->model->series->live_samples;
			devc->digital_frame_size = devc->model->series->live_samples;
			rigol_ds_set_wait_event(devc, WAIT_TRIGGER);
		} else {
			if (devc->model->series->protocol == PROTOCOL_V3) {
				if (first_frame && rigol_ds_config_set(sdi, ":WAV:MODE RAW") != SR_OK)
					return SR_ERR;
			} else if (devc->model->series->protocol >= PROTOCOL_V4) {
				num_channels = 0;

				/* Channels 3 and 4 are multiplexed with D0-7 and D8-15 */
				for (i = 0; i < devc->model->analog_channels; i++) {
					if (devc->analog_channels[i]) {
						num_channels++;
					} else if (i >= 2 && devc->model->has_digital) {
						for (j = 0; j < 8; j++) {
							if (devc->digital_channels[8 * (i - 2) + j]) {
								num_channels++;
								break;
							}
						}
					}
				}

				buffer_samples = devc->model->series->buffer_samples;
				if (first_frame && buffer_samples == 0)
				{
					/* The DS4000 series does not have a fixed memory depth, it
					 * can be chosen from the menu and also varies with number
					 * of active channels. Retrieve the actual number with the
					 * ACQ:MDEP command. */
					sr_scpi_get_int(sdi->conn, "ACQ:MDEP?", &buffer_samples);
					devc->analog_frame_size = devc->digital_frame_size =
							buffer_samples;
				}
				else if (first_frame)
				{
					/* The DS1000Z series has a fixed memory depth which we
					 * need to divide correctly according to the number of
					 * active channels. */
					devc->analog_frame_size = devc->digital_frame_size =
						num_channels == 1 ?
							buffer_samples :
								num_channels == 2 ?
									buffer_samples / 2 :
									buffer_samples / 4;
				}
			}

			if (devc->data_source == DATA_SOURCE_LIVE && rigol_ds_config_set(sdi, ":SINGL") != SR_OK)
				return SR_ERR;
			rigol_ds_set_wait_event(devc, WAIT_STOP);
			if (devc->data_source == DATA_SOURCE_SEGMENTED &&
					devc->model->series->protocol <= PROTOCOL_V4)
				if (rigol_ds_config_set(sdi, "FUNC:WREP:FCUR %d", devc->num_frames + 1) != SR_OK)
					return SR_ERR;
		}
		break;
	}

	return SR_OK;
}

/* Start reading data from the current channel */
SR_PRIV int rigol_ds_channel_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;

	if (!(devc = sdi->priv))
		return SR_ERR;

	ch = devc->channel_entry->data;

	sr_dbg("Starting reading data from channel %d", ch->index + 1);

	const gboolean first_frame = (devc->num_frames == 0);

	switch (devc->model->series->protocol) {
	case PROTOCOL_V1:
	case PROTOCOL_V2:
		if (ch->type == SR_CHANNEL_LOGIC) {
			if (sr_scpi_send(sdi->conn, ":WAV:DATA? DIG") != SR_OK)
				return SR_ERR;
		} else {
			if (sr_scpi_send(sdi->conn, ":WAV:DATA? CHAN%d",
					ch->index + 1) != SR_OK)
				return SR_ERR;
		}
		rigol_ds_set_wait_event(devc, WAIT_NONE);
		break;
	case PROTOCOL_V3:
		if (ch->type == SR_CHANNEL_LOGIC) {
			if (rigol_ds_config_set(sdi, ":WAV:SOUR LA") != SR_OK)
				return SR_ERR;
		} else {
			if (rigol_ds_config_set(sdi, ":WAV:SOUR CHAN%d",
					ch->index + 1) != SR_OK)
				return SR_ERR;
		}
		if (devc->data_source != DATA_SOURCE_LIVE) {
			if (rigol_ds_config_set(sdi, ":WAV:RES") != SR_OK)
				return SR_ERR;
			if (rigol_ds_config_set(sdi, ":WAV:BEG") != SR_OK)
				return SR_ERR;
		}
		break;
	case PROTOCOL_V4:
	case PROTOCOL_V5:
	case PROTOCOL_V6:
		if (ch->type == SR_CHANNEL_ANALOG) {
			if (rigol_ds_config_set(sdi, ":WAV:SOUR CHAN%d",
					ch->index + 1) != SR_OK)
				return SR_ERR;
		} else {
			if (rigol_ds_config_set(sdi, ":WAV:SOUR D%d",
					ch->index) != SR_OK)
				return SR_ERR;
		}

		if (first_frame && rigol_ds_config_set(sdi,
					devc->data_source == DATA_SOURCE_LIVE ?
						":WAV:MODE NORM" :":WAV:MODE RAW") != SR_OK)
			return SR_ERR;

		if (devc->data_source != DATA_SOURCE_LIVE) {
			if (rigol_ds_config_set(sdi, ":WAV:RES") != SR_OK)
				return SR_ERR;
		}
		break;
	}

	if (devc->model->series->protocol >= PROTOCOL_V3 &&
			ch->type == SR_CHANNEL_ANALOG) {
		/* Vertical increment. */
		if (first_frame && sr_scpi_get_float(sdi->conn, ":WAV:YINC?",
				&devc->vert_inc[ch->index]) != SR_OK)
			return SR_ERR;
		/* Vertical origin. */
		if (first_frame && sr_scpi_get_float(sdi->conn, ":WAV:YOR?",
			&devc->vert_origin[ch->index]) != SR_OK)
			return SR_ERR;
		/* Vertical reference. */
		if (first_frame && sr_scpi_get_int(sdi->conn, ":WAV:YREF?",
				&devc->vert_reference[ch->index]) != SR_OK)
			return SR_ERR;
	} else if (ch->type == SR_CHANNEL_ANALOG) {
		devc->vert_inc[ch->index] = devc->vdiv[ch->index] / 25.6;
	}

	rigol_ds_set_wait_event(devc, WAIT_BLOCK);

	devc->num_channel_bytes = 0;
	devc->num_header_bytes = 0;
	devc->num_block_bytes = 0;

	return SR_OK;
}

/* Read the header of a data block */
static int rigol_ds_read_header(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	struct dev_context *devc = sdi->priv;
	char *buf = (char *) devc->buffer;
	size_t header_length;
	int ret;

	/* Try to read the hashsign and length digit. */
	if (devc->num_header_bytes < 2) {
		ret = sr_scpi_read_data(scpi, buf + devc->num_header_bytes,
				2 - devc->num_header_bytes);
		if (ret < 0) {
			sr_err("Read error while reading data header.");
			return SR_ERR;
		}
		devc->num_header_bytes += ret;
	}

	if (devc->num_header_bytes < 2)
		return 0;

	if (buf[0] != '#' || !isdigit(buf[1]) || buf[1] == '0') {
		sr_err("Received invalid data block header '%c%c'.", buf[0], buf[1]);
		return SR_ERR;
	}

	header_length = 2 + buf[1] - '0';

	/* Try to read the length. */
	if (devc->num_header_bytes < header_length) {
		ret = sr_scpi_read_data(scpi, buf + devc->num_header_bytes,
				header_length - devc->num_header_bytes);
		if (ret < 0) {
			sr_err("Read error while reading data header.");
			return SR_ERR;
		}
		devc->num_header_bytes += ret;
	}

	if (devc->num_header_bytes < header_length)
		return 0;

	/* Read the data length. */
	buf[header_length] = '\0';

	if (parse_int(buf + 2, &ret) != SR_OK) {
		sr_err("Received invalid data block length '%s'.", buf + 2);
		return -1;
	}

	sr_dbg("Received data block header: '%s' -> block length %d", buf, ret);

	return ret;
}

SR_PRIV int rigol_ds_receive(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_logic logic;
	double vdiv, offset, origin;
	int len, i, vref;
	struct sr_channel *ch;
	gsize expected_data_bytes;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	scpi = sdi->conn;

	if (!(revents == G_IO_IN || revents == 0))
		return TRUE;

	const gboolean first_frame = (devc->num_frames == 0);

	switch (devc->wait_event) {
	case WAIT_NONE:
		break;
	case WAIT_TRIGGER:
		if (rigol_ds_trigger_wait(sdi) != SR_OK)
			return TRUE;
		if (rigol_ds_channel_start(sdi) != SR_OK)
			return TRUE;
		return TRUE;
	case WAIT_BLOCK:
		if (rigol_ds_block_wait(sdi) != SR_OK)
			return TRUE;
		break;
	case WAIT_STOP:
		if (rigol_ds_stop_wait(sdi) != SR_OK)
			return TRUE;
		if (rigol_ds_check_stop(sdi) != SR_OK)
			return TRUE;
		if (rigol_ds_channel_start(sdi) != SR_OK)
			return TRUE;
		return TRUE;
	default:
		sr_err("BUG: Unknown event target encountered");
		break;
	}

	ch = devc->channel_entry->data;

	expected_data_bytes = ch->type == SR_CHANNEL_ANALOG ?
			devc->analog_frame_size : devc->digital_frame_size;

	if (devc->num_block_bytes == 0) {
		if (devc->model->series->protocol >= PROTOCOL_V4) {
			if (first_frame && rigol_ds_config_set(sdi, ":WAV:START %d",
					devc->num_channel_bytes + 1) != SR_OK)
				return TRUE;
			if (first_frame && rigol_ds_config_set(sdi, ":WAV:STOP %d",
					MIN(devc->num_channel_bytes + ACQ_BLOCK_SIZE,
						devc->analog_frame_size)) != SR_OK)
				return TRUE;
		}

		if (devc->model->series->protocol >= PROTOCOL_V3) {
			if (rigol_ds_config_set(sdi, ":WAV:BEG") != SR_OK)
				return TRUE;
			if (sr_scpi_send(sdi->conn, ":WAV:DATA?") != SR_OK)
				return TRUE;
		}

		if (sr_scpi_read_begin(scpi) != SR_OK)
			return TRUE;

		if (devc->format == FORMAT_IEEE488_2) {
			sr_dbg("New block header expected");
			len = rigol_ds_read_header(sdi);
			if (len == 0)
				/* Still reading the header. */
				return TRUE;
			if (len == -1) {
				sr_err("Error while reading block header, aborting capture.");
				std_session_send_df_frame_end(sdi);
				sr_dev_acquisition_stop(sdi);
				return TRUE;
			}
			/* At slow timebases in live capture the DS2072 and
			 * DS1054Z sometimes return "short" data blocks, with
			 * apparently no way to get the rest of the data.
			 * Discard these, the complete data block will appear
			 * eventually.
			 */
			if (devc->data_source == DATA_SOURCE_LIVE
					&& (unsigned)len < expected_data_bytes) {
				sr_dbg("Discarding short data block: got %d/%d bytes\n", len, (int)expected_data_bytes);
				sr_scpi_read_data(scpi, (char *)devc->buffer, len + 1);
				devc->num_header_bytes = 0;
				return TRUE;
			}
			devc->num_block_bytes = len;
		} else {
			devc->num_block_bytes = expected_data_bytes;
		}
		devc->num_block_read = 0;
	}

	len = devc->num_block_bytes - devc->num_block_read;
	if (len > ACQ_BUFFER_SIZE)
		len = ACQ_BUFFER_SIZE;
	sr_dbg("Requesting read of %d bytes", len);

	len = sr_scpi_read_data(scpi, (char *)devc->buffer, len);

	if (len == -1) {
		sr_err("Error while reading block data, aborting capture.");
		std_session_send_df_frame_end(sdi);
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	sr_dbg("Received %d bytes.", len);

	devc->num_block_read += len;

	if (ch->type == SR_CHANNEL_ANALOG) {
		vref = devc->vert_reference[ch->index];
		vdiv = devc->vert_inc[ch->index];
		origin = devc->vert_origin[ch->index];
		offset = devc->vert_offset[ch->index];
		if (devc->model->series->protocol >= PROTOCOL_V3)
			for (i = 0; i < len; i++)
				devc->data[i] = ((int)devc->buffer[i] - vref - origin) * vdiv;
		else
			for (i = 0; i < len; i++)
				devc->data[i] = (128 - devc->buffer[i]) * vdiv - offset;
		float vdivlog = log10f(vdiv);
		int digits = -(int)vdivlog + (vdivlog < 0.0);
		sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
		analog.meaning->channels = g_slist_append(NULL, ch);
		analog.num_samples = len;
		analog.data = devc->data;
		analog.meaning->mq = SR_MQ_VOLTAGE;
		analog.meaning->unit = SR_UNIT_VOLT;
		analog.meaning->mqflags = 0;
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		g_slist_free(analog.meaning->channels);
	} else {
		logic.length = len;
		// TODO: For the MSO1000Z series, we need a way to express that
		// this data is in fact just for a single channel, with the valid
		// data for that channel in the LSB of each byte.
		logic.unitsize = devc->model->series->protocol >= PROTOCOL_V4 ? 1 : 2;
		logic.data = devc->buffer;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		sr_session_send(sdi, &packet);
	}

	if (devc->num_block_read == devc->num_block_bytes) {
		sr_dbg("Block has been completed");
		if (devc->model->series->protocol >= PROTOCOL_V3) {
			/* Discard the terminating linefeed */
			sr_scpi_read_data(scpi, (char *)devc->buffer, 1);
		}
		if (devc->format == FORMAT_IEEE488_2) {
			/* Prepare for possible next block */
			devc->num_header_bytes = 0;
			devc->num_block_bytes = 0;
			if (devc->data_source != DATA_SOURCE_LIVE)
				rigol_ds_set_wait_event(devc, WAIT_BLOCK);
		}
		if (!sr_scpi_read_complete(scpi) && !devc->channel_entry->next) {
			sr_err("Read should have been completed");
		}
		devc->num_block_read = 0;
	} else {
		sr_dbg("%" PRIu64 " of %" PRIu64 " block bytes read",
			devc->num_block_read, devc->num_block_bytes);
	}

	devc->num_channel_bytes += len;

	if (devc->num_channel_bytes < expected_data_bytes)
		/* Don't have the full data for this channel yet, re-run. */
		return TRUE;

	/* End of data for this channel. */
	if (devc->model->series->protocol == PROTOCOL_V3) {
		/* Signal end of data download to scope */
		if (devc->data_source != DATA_SOURCE_LIVE)
			/*
			 * This causes a query error, without it switching
			 * to the next channel causes an error. Fun with
			 * firmware...
			 */
			rigol_ds_config_set(sdi, ":WAV:END");
	}

	if (devc->channel_entry->next) {
		/* We got the frame for this channel, now get the next channel. */
		devc->channel_entry = devc->channel_entry->next;
		rigol_ds_channel_start(sdi);
	} else {
		/* Done with this frame. */
		std_session_send_df_frame_end(sdi);

		devc->num_frames++;

		/* V5 has no way to read the number of recorded frames, so try to set the
		 * next frame and read it back instead.
		 */
		if (devc->data_source == DATA_SOURCE_SEGMENTED &&
				devc->model->series->protocol >= PROTOCOL_V5) {
			int frames = 0;
			if (rigol_ds_config_set(sdi, "REC:CURR %d", devc->num_frames + 1) != SR_OK)
				return SR_ERR;
			if (sr_scpi_get_int(sdi->conn, "REC:CURR?", &frames) != SR_OK)
				return SR_ERR;
			devc->num_frames_segmented = frames;
		}

		if (devc->num_frames == devc->limit_frames ||
				devc->num_frames == devc->num_frames_segmented ||
				devc->data_source == DATA_SOURCE_MEMORY) {
			/* Last frame, stop capture. */
			sr_dev_acquisition_stop(sdi);
		} else {
			/* Get the next frame, starting with the first channel. */
			devc->channel_entry = devc->enabled_channels;

			rigol_ds_capture_start(sdi);

			/* Start of next frame. */
			std_session_send_df_frame_begin(sdi);
		}
	}

	return TRUE;
}

SR_PRIV int rigol_ds_get_dev_cfg(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	char *cmd;
	unsigned int i;
	int res;

	devc = sdi->priv;

	/* Analog channel state. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf(":CHAN%d:DISP?", i + 1);
		res = sr_scpi_get_bool(sdi->conn, cmd, &devc->analog_channels[i]);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
		ch = g_slist_nth_data(sdi->channels, i);
		ch->enabled = devc->analog_channels[i];
	}
	sr_dbg("Current analog channel state:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %s", i + 1, devc->analog_channels[i] ? "on" : "off");

	/* Digital channel state. */
	if (devc->model->has_digital) {
		switch (devc->model->series->protocol) {
		case PROTOCOL_V1:
		case PROTOCOL_V2:
			cmd = ":LA:DISP?";
			break;
		case PROTOCOL_V3:
		case PROTOCOL_V4:
		case PROTOCOL_V5:
			cmd = ":LA:STAT?";
			break;
		case PROTOCOL_V6:
			cmd = ":LA:ENAB?";
			break;
		}
		if (sr_scpi_get_bool(sdi->conn, cmd, &devc->la_enabled) != SR_OK)
			return SR_ERR;
		sr_dbg("Logic analyzer %s, current digital channel state:",
				devc->la_enabled ? "enabled" : "disabled");
		for (i = 0; i < ARRAY_SIZE(devc->digital_channels); i++) {
			switch (devc->model->series->protocol) {
			case PROTOCOL_V1:
			case PROTOCOL_V2:
				cmd = g_strdup_printf(":DIG%d:TURN?", i);
				break;
			case PROTOCOL_V3:
			case PROTOCOL_V4:
				cmd = g_strdup_printf(":LA:DIG%d:DISP?", i);
				break;
			case PROTOCOL_V5:
				cmd = g_strdup_printf(":LA:DISP? D%d", i);
				break;
			case PROTOCOL_V6:
				cmd = g_strdup_printf(":LA:DIG:ENAB? D%d", i);
				break;
			}
			res = sr_scpi_get_bool(sdi->conn, cmd, &devc->digital_channels[i]);
			g_free(cmd);
			if (res != SR_OK)
				return SR_ERR;
			ch = g_slist_nth_data(sdi->channels, i + devc->model->analog_channels);
			ch->enabled = devc->digital_channels[i];
			sr_dbg("D%d: %s", i, devc->digital_channels[i] ? "on" : "off");
		}
	}

	/* Timebase. */
	if (sr_scpi_get_float(sdi->conn, ":TIM:SCAL?", &devc->timebase) != SR_OK)
		return SR_ERR;
	sr_dbg("Current timebase %g", devc->timebase);

	/* Probe attenuation. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf(":CHAN%d:PROB?", i + 1);

		/* DSO1000B series prints an X after the probe factor, so
		 * we get a string and check for that instead of only handling
		 * floats. */
		char *response;
		res = sr_scpi_get_string(sdi->conn, cmd, &response);
		if (res != SR_OK)
			return SR_ERR;

		int len = strlen(response);
		if (response[len-1] == 'X')
			response[len-1] = 0;

		res = sr_atof_ascii(response, &devc->attenuation[i]);
		g_free(response);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
	}
	sr_dbg("Current probe attenuation:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %g", i + 1, devc->attenuation[i]);

	/* Vertical gain and offset. */
	if (rigol_ds_get_dev_cfg_vertical(sdi) != SR_OK)
		return SR_ERR;

	/* Coupling. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf(":CHAN%d:COUP?", i + 1);
		g_free(devc->coupling[i]);
		devc->coupling[i] = NULL;
		res = sr_scpi_get_string(sdi->conn, cmd, &devc->coupling[i]);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
	}
	sr_dbg("Current coupling:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %s", i + 1, devc->coupling[i]);

	/* Trigger source. */
	g_free(devc->trigger_source);
	devc->trigger_source = NULL;
	if (sr_scpi_get_string(sdi->conn, ":TRIG:EDGE:SOUR?", &devc->trigger_source) != SR_OK)
		return SR_ERR;
	sr_dbg("Current trigger source %s", devc->trigger_source);

	/* Horizontal trigger position. */
	if (sr_scpi_get_float(sdi->conn, devc->model->cmds[CMD_GET_HORIZ_TRIGGERPOS].str,
			&devc->horiz_triggerpos) != SR_OK)
		return SR_ERR;
	sr_dbg("Current horizontal trigger position %g", devc->horiz_triggerpos);

	/* Trigger slope. */
	g_free(devc->trigger_slope);
	devc->trigger_slope = NULL;
	if (sr_scpi_get_string(sdi->conn, ":TRIG:EDGE:SLOP?", &devc->trigger_slope) != SR_OK)
		return SR_ERR;
	sr_dbg("Current trigger slope %s", devc->trigger_slope);

	/* Trigger level. */
	if (sr_scpi_get_float(sdi->conn, ":TRIG:EDGE:LEV?", &devc->trigger_level) != SR_OK)
		return SR_ERR;
	sr_dbg("Current trigger level %g", devc->trigger_level);

	return SR_OK;
}

SR_PRIV int rigol_ds_get_dev_cfg_vertical(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	char *cmd;
	unsigned int i;
	int res;

	devc = sdi->priv;

	/* Vertical gain. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf(":CHAN%d:SCAL?", i + 1);
		res = sr_scpi_get_float(sdi->conn, cmd, &devc->vdiv[i]);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
	}
	sr_dbg("Current vertical gain:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %g", i + 1, devc->vdiv[i]);

	/* Vertical offset. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf(":CHAN%d:OFFS?", i + 1);
		res = sr_scpi_get_float(sdi->conn, cmd, &devc->vert_offset[i]);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
	}
	sr_dbg("Current vertical offset:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %g", i + 1, devc->vert_offset[i]);

	return SR_OK;
}
