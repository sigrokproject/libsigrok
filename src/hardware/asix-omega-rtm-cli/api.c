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

/*
 * This sigrok driver implementation uses the vendor's CLI application
 * for the ASIX OMEGA to operate the device in real time mode. The
 * external process handles the device detection, USB communication
 * (FTDI FIFO), FPGA netlist download, and device control. The process'
 * stdout provides a continuous RLE compressed stream of 16bit samples
 * taken at 200MHz.
 *
 * Known limitations: The samplerate is fixed. Hardware triggers are not
 * available in this mode. The start of the acquisition takes a few
 * seconds, but the device's native protocol is unknown and its firmware
 * is unavailable, so that a native sigrok driver is in some distant
 * future. Users need to initiate the acquisition in sigrok early so
 * that the device is capturing when the event of interest happens.
 *
 * The vendor application's executable either must be named omegartmcli
 * and must be found in PATH, or the OMEGARTMCLI environment variable
 * must contain its location. A scan option could be used when a
 * suitable SR_CONF key gets identified which communicates executable
 * locations.
 *
 * When multiple devices are connected, then a conn=sn=... specification
 * can select one of the devices. The serial number should contain six
 * or eight hex digits (this follows the vendor's approach for the CLI
 * application).
 */

/*
 * Implementor's notes. Examples of program output which gets parsed by
 * this sigrok driver.
 *
 *   $ ./omegartmcli.exe -version
 *   omegartmcli.exe Omega Real-Time Mode
 *   Version 2016-12-14
 *   Copyright (c) 1991-2016 ASIX s.r.o.
 *   Email: support@asix.net
 *
 *   $ ./omegartmcli.exe -bin [-serial SERNO] <NULL>
 *   (five command line words including the terminator)
 *
 * The RTM CLI application terminates when its stdin closes, or when
 * CTRL-C is pressed. The former is more portable across platforms. The
 * stderr output should get ignored, it's rather noisy here under wine,
 * communicates non-fatal diagnostics, and may communicate "progress"
 * which we don't care about.
 *
 * Ideally the external process could get started earlier, and gets
 * re-used across several sigrok acquisition activities. Unfortunately
 * the driver's open/close actions lack a sigrok session, and cannot
 * register the receive callback (or needs to duplicate common support
 * code). When such an approach gets implemented, the external process'
 * output must get drained even outside of sigrok acquisition phases,
 * the cost of which is yet to get determined (depends on the input
 * signals, may be expensive).
 *
 * The binary data format is used to reduce the amount of inter process
 * communication. The format is rather simple: Three 16bit items (LE
 * format) carry a timestamp (10ns resolution), and two 16bit samples
 * (taken at 5ns intervals). The timestamp may translate to a repetition
 * of the last sample a given number of times (RLE compression of idle
 * phases where inputs don't change). The first timestamp after program
 * startup is to get ignored. Chunks are sent after at most 32Ki 10ns
 * ticks, to not overflow the 16bit counter. Which translates to a data
 * volume of 6 bytes each 328us for idle inputs, higher for changing
 * input signals.
 *
 * Is it useful to implement a set of samplerates in the sigrok driver,
 * and downsample the data which is provided by the Asix application?
 * This would not avoid the pressure of receiving the acquisition
 * process' output, but may result in reduced cost on the sigrok side
 * when users want to inspect slow signals, or export to "expensive"
 * file formats.
 *
 * This driver implementation may benefit from software trigger support.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include "protocol.h"

static const char *channel_names[] = {
	"1", "2", "3", "4", "5", "6", "7", "8",
	"9", "10", "11", "12", "13", "14", "15", "16",
};

static const uint64_t samplerates[] = {
	SR_MHZ(200),
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN, /* Accepts serial number specs. */
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_LIST,
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *conn, *serno, *exe;
	GSList *devices;
	size_t argc, chidx;
	gchar **argv, *output, *vers_text, *eol;
	GSpawnFlags flags;
	GError *error;
	gboolean ok;
	char serno_buff[10];
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	/* Extract optional serial number from conn= spec. */
	conn = NULL;
	(void)sr_serial_extract_options(options, &conn, NULL);
	if (!conn || !*conn)
		conn = NULL;
	serno = NULL;
	if (conn) {
		if (!g_str_has_prefix(conn, "sn=")) {
			sr_err("conn= must specify a serial number.");
			return NULL;
		}
		serno = conn + strlen("sn=");
		if (!*serno)
			serno = NULL;
	}
	if (serno)
		sr_dbg("User specified serial number: %s", serno);
	if (serno && strlen(serno) == 4) {
		sr_dbg("Adding 03 prefix to user specified serial number");
		snprintf(serno_buff, sizeof(serno_buff), "03%s", serno);
		serno = serno_buff;
	}
	if (serno && strlen(serno) != 6 && strlen(serno) != 8) {
		sr_err("Serial number must be 03xxxx or A603xxxx");
		serno = NULL;
	}

	devices = NULL;

	/*
	 * Check availability of the external executable. Notice that
	 * failure is non-fatal, the scan can take place even when users
	 * don't request and don't expect to use Asix Omega devices.
	 */
	exe = getenv("OMEGARTMCLI");
	if (!exe || !*exe)
		exe = "omegartmcli";
	sr_dbg("Vendor application executable: %s", exe);
	argv = g_malloc0(5 * sizeof(argv[0]));
	argc = 0;
	argv[argc++] = g_strdup(exe);
	argv[argc++] = g_strdup("-version");
	argv[argc++] = NULL;
	flags = G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL;
	output = NULL;
	error = NULL;
	ok = g_spawn_sync(NULL, argv, NULL, flags, NULL, NULL,
		&output, NULL, NULL, &error);
	if (error && error->code != G_SPAWN_ERROR_NOENT)
		sr_err("Cannot execute RTM CLI process: %s", error->message);
	if (error) {
		ok = FALSE;
		g_error_free(error);
	}
	if (!output || !*output)
		ok = FALSE;
	if (!ok) {
		sr_dbg("External RTM CLI execution failed.");
		g_free(output);
		g_strfreev(argv);
		return NULL;
	}

	/*
	 * Get the executable's version from second stdout line. This
	 * only executes when the executable is found, failure to get
	 * the version information is considered fatal.
	 */
	vers_text = strstr(output, "Version ");
	if (!vers_text)
		ok = FALSE;
	if (ok) {
		vers_text += strlen("Version ");
		eol = strchr(vers_text, '\n');
		if (eol)
			*eol = '\0';
		eol = strchr(vers_text, '\r');
		if (eol)
			*eol = '\0';
		if (!vers_text || !*vers_text)
			ok = FALSE;
	}
	if (!ok) {
		sr_err("Cannot get RTM CLI executable's version.");
		g_free(output);
		g_strfreev(argv);
		return NULL;
	}
	sr_info("RTM CLI executable version: %s", vers_text);

	/*
	 * Create a device instance, add it to the result set. Create a
	 * device context. Change the -version command into the command
	 * for acquisition for later use in the driver's lifetime.
	 */
	sdi = g_malloc0(sizeof(*sdi));
	devices = g_slist_append(devices, sdi);
	sdi->status = SR_ST_INITIALIZING;
	sdi->vendor = g_strdup("ASIX");
	sdi->model = g_strdup("OMEGA RTM CLI");
	sdi->version = g_strdup(vers_text);
	if (serno)
		sdi->serial_num = g_strdup(serno);
	if (conn)
		sdi->connection_id = g_strdup(conn);
	for (chidx = 0; chidx < ARRAY_SIZE(channel_names); chidx++) {
		sr_channel_new(sdi, chidx, SR_CHANNEL_LOGIC,
			TRUE, channel_names[chidx]);
	}

	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	sr_sw_limits_init(&devc->limits);
	argc = 1;
	g_free(argv[argc]);
	argv[argc++] = g_strdup("-bin");
	if (serno) {
		argv[argc++] = g_strdup("-serial");
		argv[argc++] = g_strdup(serno);
	}
	argv[argc++] = NULL;
	devc->child.argv = argv;
	devc->child.flags = flags | G_SPAWN_CLOEXEC_PIPES;
	devc->child.fd_stdin_write = -1;
	devc->child.fd_stdout_read = -1;

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->connection_id)
			return SR_ERR_NA;
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(samplerates[0]);
		break;
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		if (cg)
			return SR_ERR_NA;
		return STD_CONFIG_LIST(key, data, sdi, cg,
			scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	int fd, events;
	uint64_t remain_count;

	devc = sdi->priv;

	/* Start the external acquisition process. */
	ret = omega_rtm_cli_open(sdi);
	if (ret != SR_OK)
		return ret;
	fd = devc->child.fd_stdout_read;
	events = G_IO_IN | G_IO_ERR;

	/*
	 * Start supervising acquisition limits. Arrange for a stricter
	 * "samples count" check than supported by the common approach.
	 */
	sr_sw_limits_acquisition_start(&devc->limits);
	ret = sr_sw_limits_get_remain(&devc->limits,
		&remain_count, NULL, NULL, NULL);
	if (ret != SR_OK)
		return ret;
	if (remain_count) {
		devc->samples.remain_count = remain_count;
		devc->samples.check_count = TRUE;
	}

	/* Send the session feed header. */
	ret = std_session_send_df_header(sdi);
	if (ret != SR_OK)
		return ret;

	/* Start processing the external process' output. */
	ret = sr_session_source_add(sdi->session, fd, events, 10,
		omega_rtm_cli_receive_data, (void *)sdi); /* Un-const. */
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	int fd;

	devc = sdi->priv;

	/*
	 * Implementor's note: Do run all stop activities even if
	 * some of them may fail. Emit diagnostics messages as errors
	 * are seen, but don't return early.
	 */

	/* Stop processing the external process' output. */
	fd = devc->child.fd_stdout_read;
	if (fd >= 0) {
		ret = sr_session_source_remove(sdi->session, fd);
		if (ret != SR_OK) {
			sr_err("Cannot stop reading acquisition data");
		}
	}

	ret = std_session_send_df_end(sdi);
	(void)ret;

	ret = omega_rtm_cli_close(sdi);
	if (ret != SR_OK) {
		sr_err("Could not terminate acquisition process");
	}
	(void)ret;

	return SR_OK;
}

static struct sr_dev_driver asix_omega_rtm_cli_driver_info = {
	.name = "asix-omega-rtm-cli",
	.longname = "ASIX OMEGA RTM CLI",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_dummy_dev_open,
	.dev_close = std_dummy_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(asix_omega_rtm_cli_driver_info);
