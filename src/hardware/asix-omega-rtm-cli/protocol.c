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
#include <unistd.h>

#include "protocol.h"

/*
 * Start the external acquisition process (vendor's CLI application).
 * Get the initial response to verify its operation.
 */
SR_PRIV int omega_rtm_cli_open(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	gboolean ok;
	gchar **argv;
	GSpawnFlags flags;
	GPid pid;
	gint fd_in, fd_out;
	GError *error;
	GString *txt;
	ssize_t rcvd;
	uint8_t rsp[3 * sizeof(uint16_t)];
	const uint8_t *rdptr;
	uint16_t stamp, sample1, sample2;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	if (devc->child.running) {
		sr_err("Vendor application already running.");
		return SR_ERR_BUG;
	}

	/* Prepare to feed sample data to the session. */
	memset(&devc->rawdata, 0, sizeof(devc->rawdata));
	memset(&devc->samples, 0, sizeof(devc->samples));
	devc->samples.queue = feed_queue_logic_alloc(sdi,
		FEED_QUEUE_DEPTH, sizeof(devc->samples.last_sample));

	/*
	 * Start the background process. May take considerable time
	 * before actual acquisition starts.
	 */
	sr_dbg("Starting vendor application");
	argv = devc->child.argv;
	flags = devc->child.flags;
	error = NULL;
	ok = g_spawn_async_with_pipes(NULL, argv, NULL, flags, NULL, NULL,
		&pid, &fd_in, &fd_out, NULL, &error);
	if (error) {
		sr_err("Cannot execute RTM CLI process: %s", error->message);
		g_error_free(error);
		ok = FALSE;
	}
	if (fd_in < 0 || fd_out < 0)
		ok = FALSE;
	if (!ok) {
		sr_err("Vendor application start failed.");
		return SR_ERR_IO;
	}
	devc->child.pid = pid;
	devc->child.fd_stdin_write = fd_in;
	devc->child.fd_stdout_read = fd_out;
	devc->child.running = TRUE;
	sr_dbg("Started vendor application, in %d, out %d", fd_in, fd_out);
	txt = sr_hexdump_new((const uint8_t *)&pid, sizeof(pid));
	sr_dbg("Vendor application PID (OS dependent): %s", txt->str);
	sr_hexdump_free(txt);

	/*
	 * Get the initial response. Verifies its operation, and only
	 * returns with success when acquisition became operational.
	 */
	rcvd = read(fd_out, rsp, sizeof(rsp));
	sr_dbg("Read from vendor application, ret %zd", rcvd);
	if (rcvd < 0)
		ok = FALSE;
	if (ok && (size_t)rcvd != sizeof(rsp))
		ok = FALSE;
	if (!ok) {
		omega_rtm_cli_close(sdi);
		return SR_ERR_IO;
	}

	/*
	 * Ignore the first timestamp. Grab the most recent sample data
	 * to feed the session from it upon later repetition.
	 */
	rdptr = rsp;
	stamp = read_u16le_inc(&rdptr);
	sample1 = read_u16le_inc(&rdptr);
	sample2 = read_u16le_inc(&rdptr);
	sr_dbg("stamp %u, samples %x %x", stamp, sample1, sample2);
	write_u16le(devc->samples.last_sample, sample2);

	return SR_OK;
}

/*
 * Terminate the external acquisition process (vendor's CLI application).
 */
SR_PRIV int omega_rtm_cli_close(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/* Close the external process' stdin. Discard its stdout. */
	sr_dbg("Closing vendor application file descriptors.");
	if (devc->child.fd_stdin_write >= 0) {
		sr_dbg("Closing vendor application stdin descriptor.");
		close(devc->child.fd_stdin_write);
		devc->child.fd_stdin_write = -1;
	}
	if (devc->child.fd_stdout_read >= 0) {
		sr_dbg("Closing vendor application stdout descriptor.");
		close(devc->child.fd_stdout_read);
		devc->child.fd_stdout_read = -1;
	}

	/* Terminate the external process. */
	if (devc->child.running) {
		sr_dbg("Closing vendor application process.");
		(void)g_spawn_close_pid(devc->child.pid);
		memset(&devc->child.pid, 0, sizeof(devc->child.pid));
		devc->child.running = FALSE;
	}

	/* Release the session feed queue. */
	if (devc->samples.queue) {
		feed_queue_logic_free(devc->samples.queue);
		devc->samples.queue = NULL;
	}

	sr_dbg("Done closing vendor application.");

	return SR_OK;
}

/*
 * Process received sample data, which comes in 6-byte chunks.
 * Uncompress the RLE stream. Strictly enforce user specified sample
 * count limits in the process, cap the submission when an uncompressed
 * chunk would exceed the limit.
 */
static int omega_rtm_cli_process_rawdata(const struct sr_dev_inst *sdi)
{
	static const size_t chunk_size = 3 * sizeof(uint16_t);

	struct dev_context *devc;
	const uint8_t *rdptr;
	size_t avail, taken, count;
	uint16_t stamp, sample1, sample2;
	int ret;

	devc = sdi->priv;
	rdptr = &devc->rawdata.buff[0];
	avail = devc->rawdata.fill;
	taken = 0;
	ret = SR_OK;

	/* Cope with previous errors, silently discard RX data. */
	if (!devc->samples.queue)
		ret = SR_ERR_DATA;

	/* Process those chunks whose reception has completed. */
	while (ret == SR_OK && avail >= chunk_size) {
		stamp = read_u16le_inc(&rdptr);
		sample1 = read_u16le_inc(&rdptr);
		sample2 = read_u16le_inc(&rdptr);
		avail -= chunk_size;
		taken += chunk_size;

		/*
		 * Uncompress the RLE stream by repeating the last
		 * sample value when necessary. Notice that the stamp
		 * has a resolution of 10ns and thus covers two times
		 * the number of samples, these are taken each 5ns (at
		 * 200MHz rate). A stamp value of 1 is immediately
		 * adjacent to the last chunk.
		 */
		if (stamp)
			stamp--;
		count = stamp * 2;
		if (devc->samples.check_count) {
			if (count > devc->samples.remain_count)
				count = devc->samples.remain_count;
			devc->samples.remain_count -= count;
		}
		if (count) {
			ret = feed_queue_logic_submit(devc->samples.queue,
				devc->samples.last_sample, count);
			if (ret != SR_OK)
				break;
			sr_sw_limits_update_samples_read(&devc->limits, count);
		}
		if (devc->samples.check_count && !devc->samples.remain_count)
			break;

		/*
		 * Also send the current samples. Keep the last value at
		 * hand because future chunks might repeat it.
		 */
		write_u16le(devc->samples.last_sample, sample1);
		ret = feed_queue_logic_submit(devc->samples.queue,
			devc->samples.last_sample, 1);
		if (ret != SR_OK)
			break;

		write_u16le(devc->samples.last_sample, sample2);
		ret = feed_queue_logic_submit(devc->samples.queue,
			devc->samples.last_sample, 1);
		if (ret != SR_OK)
			break;

		count = 2;
		sr_sw_limits_update_samples_read(&devc->limits, count);
		if (devc->samples.check_count) {
			if (count > devc->samples.remain_count)
				count = devc->samples.remain_count;
			devc->samples.remain_count -= count;
			if (!devc->samples.remain_count)
				break;
		}
	}

	/*
	 * Silently consume all chunks which were successfully received.
	 * These either completely got processed, or we are in an error
	 * path and discard unprocessed but complete sample data before
	 * propagating the error condition. This simplifies the logic
	 * above, and allows to keep draining the acquisition process'
	 * output, perhaps even resynchronize to it in a later attempt.
	 * The cost of this rare operation does not matter, robustness
	 * does.
	 */
	while (avail >= chunk_size) {
		avail -= chunk_size;
		taken += chunk_size;
	}

	/*
	 * Shift remainders (incomplete chunks) down to the start of the
	 * receive buffer.
	 */
	if (taken && avail) {
		memmove(&devc->rawdata.buff[0],
			&devc->rawdata.buff[taken], avail);
	}
	devc->rawdata.fill -= taken;

	return ret;
}

SR_PRIV int omega_rtm_cli_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	uint8_t *buff;
	size_t space;
	ssize_t rcvd;
	int ret;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	devc = sdi->priv;
	if (!devc)
		return TRUE;

	/* Process receive data when available. */
	if (revents & G_IO_IN) do {
		buff = &devc->rawdata.buff[devc->rawdata.fill];
		space = sizeof(devc->rawdata.buff) - devc->rawdata.fill;
		rcvd = read(fd, buff, space);
		sr_spew("Read from vendor application, ret %zd", rcvd);
		if (rcvd <= 0)
			break;
		devc->rawdata.fill += (size_t)rcvd;
		ret = omega_rtm_cli_process_rawdata(sdi);
		if (ret != SR_OK) {
			sr_err("Could not process sample data.");
		}
	} while (0);

	/* Handle receive errors. */
	if (revents & G_IO_ERR) {
		(void)feed_queue_logic_flush(devc->samples.queue);
		(void)sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
	}

	/* Handle optional acquisition limits. */
	if (sr_sw_limits_check(&devc->limits)) {
		(void)feed_queue_logic_flush(devc->samples.queue);
		(void)sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
	}

	return TRUE;
}
