// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Enrico Scholz <enrico.scholz@ensc.de>
 */

#include <config.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libsigrok/libsigrok.h>
#include <libsigrok-internal.h>

#include "protocol.h"

#define BIT(_b)		(1ul << (_b))

/* Describes the power supply model */
struct peaktech_model_desc {
	char const			*name;
	enum peaktech_model		model;
	unsigned int			num_chan;
	/* size of an INQUIRY reply */
	size_t				reply_size;
};

static struct peaktech_model_desc const	MODELS[] = {
	[PEAKTECH_MODEL_6070]		= {
		.name			= "6070",
		.model			= PEAKTECH_MODEL_6070,
		.num_chan		= 1,
		.reply_size		= sizeof(struct pt_6070_proto_inquire_reply),
	},
	[PEAKTECH_MODEL_6075]		= {
		.name			= "6075",
		.model			= PEAKTECH_MODEL_6075,
		.num_chan		= 2,
		.reply_size		= sizeof(struct pt_6075_proto_inquire_reply),
	}
};

enum {
	PEAKTECH_CHAN_CTRL_VOLT,
	PEAKTECH_CHAN_CTRL_CURR,
};

struct peaktech_chan_parm {
	gdouble				min;
	gdouble				max;
	gdouble				step;
};

static struct peaktech_chan_parm const	CHAN_PARM[] = {
	[PEAKTECH_CHAN_CTRL_VOLT]	= {
		.min			= 0,
		.max			= 30,
		.step			= 0.01,
	},
	[PEAKTECH_CHAN_CTRL_CURR]	= {
		.min			= 0,
		.max			= 5,
		.step			= 0.001,
	},
};

static uint32_t const			SCANOPTS[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
	SR_CONF_FORCE_DETECT,
};

static uint32_t const			DRVOPTS[] = {
	SR_CONF_POWER_SUPPLY,
};

static uint32_t const			DEVOPTS[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CHANNEL_CONFIG | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static uint32_t const			DEVOPTS_CG[] = {
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_REGULATION | SR_CONF_GET,
};

static char const * const		CHANNEL_MODES[] = {
	[PEAKTECH_CHAN_MODE_INDEPEDENT] = "Independent",
	[PEAKTECH_CHAN_MODE_SERIES]     = "Series",
	[PEAKTECH_CHAN_MODE_PARALLEL]   = "Parallel",
};

static char const * const		REGULATION_MODES[] = {
	"CC", "CV",
};

/* dirty markers both for the whole device and for channel groups */
enum {
	PEAKTECH_DIRTY_VOLT,
	PEAKTECH_DIRTY_CURR,

	PEAKTECH_DIRTY_CHAN_MODE,
	PEAKTECH_DIRTY_CONFIG,
	PEAKTECH_DIRTY_OUTPUT,
};

/* dynamic device data */
struct peaktech_device_data {
	unsigned int		volt;
	unsigned int		curr;
	/* TODO: logic below assumes that either _cv or _cc is set; enforce
	 * this constraint somehow in the structure */
	bool			output_cv;
	bool			output_cc;
	bool			output_ena;
};

/* setup data which is either configured by sigrok or on the device. */
struct peaktech_device_sdata {
	unsigned int		volt;
	unsigned int		curr;
};

struct peaktech_chan_config {
	struct peaktech_device_data	dev;
	struct peaktech_device_sdata	set;

	unsigned long			dirty;
	unsigned long			cleanup;
};

enum peaktech_state {
	PEAKTECH_STATE_INIT,
	PEAKTECH_STATE_ERR,
	PEAKTECH_STATE_SEND,
	PEAKTECH_STATE_EXPECT_INQUIRY,
	PEAKTECH_STATE_EXPECT_CONFIRM,
};

struct peaktech_report {
	unsigned int				data[PEAKTECH_MAX_CHAN];
	struct sr_analog_meaning		meaning;
	struct sr_analog_encoding		encoding;
	struct sr_analog_spec			spec;
	struct sr_datafeed_analog		analog;
	struct sr_datafeed_packet		packet;
};

struct peaktech_device {
	struct sr_dev_inst			*sdi;
	struct peaktech_model_desc const	*model;
	GSList					*ch_volt;
	GSList					*ch_curr;

	/* acquisition is running; will be used e.g. by the config_[sg]et()
	 * functions to run blocking operations on the serial bus */
	bool					acq_running;

	/* active acquisition callback */
	sr_receive_data_callback		cb;

	/* internal state during acquisition */
	enum peaktech_state			state;

	/* device setting; marked by PEAKTECH_DIRTY_CHAN_MODE as dirty */
	enum peaktech_chan_mode			chan_mode;
	/* device setting; marked by PEAKTECH_DIRTY_OUTPUT as dirty */
	bool					output_ena;

	/* channel config; only model->num_chan entries are valid */
	struct peaktech_chan_config		config[PEAKTECH_MAX_CHAN];

	/* dirty flags for device settings; will be set in config_set()
	 * callbacks and cleared after confirmation has been received. */
	unsigned long				dirty;

	/* when doing async requests during acquisition, it stores the dirty
	 * flag which is going to be cleared */
	unsigned long				cleanup;

	union pt_proto_generic_req		send_buf;
	size_t					send_pos;
	size_t					send_len;

	union pt_proto_generic_reply		recv_buf;
	size_t					recv_pos;

	struct peaktech_report			report_curr;
	struct peaktech_report			report_volt;
};

/**
 *  Blocking serial communication helper.
 *
 *  It sends data, receives a response and checks for its validity:
 *
 *  - response must have exactly a length of in_len
 *  - crc must be valid
 *
 *  Returns an SR_ERR_xxx error code
 */
static int pt_serial_send_recv(struct sr_serial_dev_inst *serial,
			       void const *data_out, size_t out_len,
			       void *data_in, size_t in_len)
{
	unsigned int			delay_ms;
	int				rc;

	delay_ms = serial_timeout(serial, out_len) + 25;
	rc = serial_write_blocking(serial, data_out, out_len, delay_ms);
	if (rc < 0 || (size_t)rc != out_len) {
		sr_err("Unable to send data in blocking mode: %d", rc);
		return rc < 0 ? rc : SR_ERR_TIMEOUT;
	}

	/* when receiving the input, we have to wait until data has been
	 * transmitted physically and the full response has been received plus
	 * some processing time on the device. */
	delay_ms += serial_timeout(serial, in_len) + 25;
	rc = serial_read_blocking(serial, data_in, in_len, delay_ms);
	if (rc < 0) {
		sr_err("Unable to read data in blocking mode: %s",
		       sr_strerror(rc));
		return rc;
	}

	if ((size_t)rc != in_len) {
		sr_err("unexpected number of data read: %d vs %zu", rc, in_len);
		return SR_ERR_TIMEOUT;
	}

	if (!peaktech_607x_proto_crc_check(data_in, in_len)) {
		sr_warn("crc error in read data");
		return SR_ERR_IO;
	}

	return SR_OK;
}

/**
 *  Blocking serial communication helper for sending setup requests.
 *
 *  It sends a setup request, receives its confirmation and checks for its
 *  validity:
 *
 *  - checks from pt_serial_send_recv() must succeed
 *  - response must match the request
 *
 *  Returns an SR_ERR_xxx error code
 */
static int pt_serial_send_setup(struct sr_serial_dev_inst *serial,
				struct pt_proto_setup_req *req)
{
	unsigned char	tmp[sizeof *req];
	int		rc;

	rc = pt_serial_send_recv(serial, req, sizeof *req, tmp, sizeof tmp);
	if (rc < 0) {
		sr_err("failed to send command: %d", rc);
		return rc;
	}

	if (memcmp(req, tmp, sizeof tmp) != 0) {
		sr_err("confirmation differs");
		return SR_ERR_IO;
	}

	return SR_OK;
}

static int pt_serial_send_setup_op(char const *op,
				   struct sr_serial_dev_inst *serial,
				   struct pt_proto_setup_req *req)
{
	int			rc;

	sr_dbg("sending %s request", op);
	rc = pt_serial_send_setup(serial, req);
	if (rc < 0)
		sr_err("%s failed: %d", op, rc);

	return rc;
}

/**
 *  Blocking serial communication helper for sending inquiries.
 *
 *  Function constructs an inquiry requests, receives the results and checks
 *  for its validity:
 *
 *  - checks from pt_serial_send_recv() must succeed
 *
 *  Returns an SR_ERR_xxx error code
 */
static int pt_serial_send_inquiry(struct peaktech_model_desc const *model,
				  struct sr_serial_dev_inst *serial,
				  union pt_proto_inquire_reply *reply)
{
	struct pt_proto_inquire_req const	req =
		pt_proto_inquire_req(model->model);
	int					rc;

	g_assert(model->reply_size <= sizeof *reply);

	rc = pt_serial_send_recv(serial, &req, sizeof req,
				 reply, model->reply_size);
	if (rc < 0) {
		sr_err("inquire request failed");
		return rc;
	}

	return SR_OK;
}

static void pt_inquiry_apply_set(struct peaktech_chan_config *config,
				 be16_t volt, be16_t curr)
{
	/* update the 'set' parameters only when they are not marked as dirty.
	 * Else, an INQUIRY result might override locally requested
	 * changes. */
	if (!(config->dirty & BIT(PEAKTECH_DIRTY_VOLT)))
		config->set.volt = be16_to_cpu(volt);

	if (!(config->dirty & BIT(PEAKTECH_DIRTY_CURR)))
		config->set.curr = be16_to_cpu(curr);
}

/**
 *  Parses and inquiry result and updates device configuration data
 */
static void pt_inquiry_parse(struct peaktech_device *devc, void const *data_)
{
	union pt_proto_inquire_reply const	*data = data_;

	switch (devc->model->model) {
	case PEAKTECH_MODEL_6070:
		devc->config[0].dev = (struct peaktech_device_data) {
			.volt	    = be16_to_cpu(data->p6070.ch1_volt),
			.curr	    = be16_to_cpu(data->p6070.ch1_curr),
			.output_ena = !!(data->p6070.ch1_status & BIT(5)),
			.output_cv  = !!(data->p6070.ch1_status & BIT(0)),
			.output_cc  = !!(data->p6070.ch1_status & BIT(1)),
		};

		pt_inquiry_apply_set(&devc->config[0],
				     data->p6070.ch1_volt_set,
				     data->p6070.ch1_curr_set);

		if (!(devc->dirty & BIT(PEAKTECH_DIRTY_OUTPUT)))
			devc->output_ena = devc->config[0].dev.output_ena;

		break;

	case PEAKTECH_MODEL_6075:
		/* TODO: this has not been not verified! */
		devc->config[0].dev = (struct peaktech_device_data) {
			.volt	    = be16_to_cpu(data->p6075.ch1_volt),
			.curr	    = be16_to_cpu(data->p6075.ch1_curr),
			.output_ena = !!(data->p6075.ch1_status & BIT(5)),
			.output_cv  = !!(data->p6075.ch1_status & BIT(0)),
			.output_cc  = !!(data->p6075.ch1_status & BIT(1)),
		};

		/* TODO: this has not been not verified! */
		devc->config[1].dev = (struct peaktech_device_data) {
			.volt	    = be16_to_cpu(data->p6075.ch2_volt),
			.curr	    = be16_to_cpu(data->p6075.ch2_curr),
			.output_ena = !!(data->p6075.ch2_status & BIT(5)),
			.output_cv  = !!(data->p6075.ch2_status & BIT(0)),
			.output_cc  = !!(data->p6075.ch2_status & BIT(1)),
		};

		pt_inquiry_apply_set(&devc->config[0],
				     data->p6075.ch1_volt_set,
				     data->p6075.ch1_curr_set);

		pt_inquiry_apply_set(&devc->config[1],
				     data->p6075.ch2_volt_set,
				     data->p6075.ch2_curr_set);

		/* TODO: is it possible that config[1] has other settings? */
		if (!(devc->dirty & BIT(PEAKTECH_DIRTY_OUTPUT)))
			devc->output_ena = devc->config[0].dev.output_ena;

		/* TODO: handle 'ser' + 'par' status bits */
		break;
	}
}

/**
 *  Reports device data.
 */
static void pt_inquiry_report(struct peaktech_device *devc)
{
	struct sr_dev_inst	*sdi = devc->sdi;

	for (size_t i = 0; i < devc->model->num_chan; ++i) {
		devc->report_volt.data[i]  = devc->config[i].dev.volt;
		devc->report_curr.data[i]  = devc->config[i].dev.curr;
	}

	std_session_send_df_frame_begin(sdi);

	sr_session_send(sdi, &devc->report_volt.packet);
	sr_session_send(sdi, &devc->report_curr.packet);

	std_session_send_df_frame_end(sdi);
}

/**
 *  Applies setup in non-aquisition mode.
 *
 *  For each dirty flag, it sends the corresponding request and clears the
 *  flag.  One call to this function requests a change of *all* dirty values.
 */
static int pt_setup_apply(struct peaktech_device *devc)
{
	struct sr_serial_dev_inst	*serial = devc->sdi->conn;
	enum peaktech_model		model = devc->model->model;
	int				rc;

	g_assert(!devc->acq_running);

	if (devc->dirty & BIT(PEAKTECH_DIRTY_CHAN_MODE)) {
		struct pt_proto_setup_req		req =
			pt_proto_chan_mode_req(model, devc->chan_mode);

		rc = pt_serial_send_setup_op("CHAN_MODE", serial, &req);
		if (rc < 0)
			return rc;

		devc->dirty &= ~BIT(PEAKTECH_DIRTY_CHAN_MODE);
	}

	if (devc->dirty & BIT(PEAKTECH_DIRTY_OUTPUT)) {
		struct pt_proto_setup_req		req =
			pt_proto_output_en_req(model, devc->output_ena);

		rc = pt_serial_send_setup_op("OUTPUT_EN", serial, &req);
		if (rc < 0)
			return rc;

		devc->dirty &= ~BIT(PEAKTECH_DIRTY_OUTPUT);
	}

	for (size_t i = 0; i < ARRAY_SIZE(devc->config); ++i) {
		struct peaktech_chan_config	*cfg = &devc->config[i];
		char				op[64];

		if (cfg->dirty & BIT(PEAKTECH_DIRTY_VOLT)) {
			struct pt_proto_setup_req		req =
				pt_proto_volt_set_req(model, i, cfg->set.volt);

			sprintf(op, "VOLT_SET@%zd", i);

			rc = pt_serial_send_setup_op(op, serial, &req);
			if (rc < 0)
				return rc;

			cfg->dirty &= ~BIT(PEAKTECH_DIRTY_VOLT);
		}

		if (cfg->dirty & BIT(PEAKTECH_DIRTY_CURR)) {
			struct pt_proto_setup_req		req =
				pt_proto_curr_set_req(model, i, cfg->set.curr);

			sprintf(op, "CURR_SET@%zd", i);

			rc = pt_serial_send_setup_op(op, serial, &req);
			if (rc < 0)
				return rc;

			cfg->dirty &= ~BIT(PEAKTECH_DIRTY_CURR);
		}
	}

	return SR_OK;
}

/**
 *  Helper function to cast an integer value to double and scale it
 */
static GVariant *scaled_double(unsigned int v, unsigned int scale)
{
	double	res = v;

	res /= scale;

	return g_variant_new_double(res);
}

static struct peaktech_chan_config *
get_chan_config(struct sr_channel_group const *cg)
{
	struct peaktech_chan_config		**cfg = cg->priv;

	return *cfg;
}

/**
 *  libsigrok sr_dev_driver::config_get() api function
 */
static int peaktech_config_get(uint32_t key, GVariant **data,
			       struct sr_dev_inst const *sdi,
			       struct sr_channel_group const *cg)
{
	struct peaktech_device		*devc = sdi ? sdi->priv : NULL;
	int				rc;

	if (!devc)
		return SR_ERR_ARG;

	/* when acquisition is not running, read the values from the device */
	if (!devc->acq_running) {
		union pt_proto_inquire_reply	reply;

		rc = pt_serial_send_inquiry(devc->model, sdi->conn, &reply);
		if (rc < 0) {
			sr_err("failed to run inquire: %d", rc);
			return rc;
		}

		pt_inquiry_parse(devc, &reply);
	}

	if (!cg) {
		/* handle global configuration options */
		switch (key) {
		case SR_CONF_CHANNEL_CONFIG:
			*data = g_variant_new_string(CHANNEL_MODES[devc->chan_mode]);
			break;
		case SR_CONF_ENABLED:
			*data = g_variant_new_boolean(devc->output_ena);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* handle channel group configuration options */
		struct peaktech_chan_config const	*cfg = get_chan_config(cg);

		switch (key) {
		case SR_CONF_VOLTAGE:
			*data = scaled_double(cfg->dev.volt, 100);
			break;
		case SR_CONF_VOLTAGE_TARGET:
			*data = scaled_double(cfg->set.volt, 100);
			break;
		case SR_CONF_CURRENT:
			*data = scaled_double(cfg->dev.curr, 1000);
			break;
		case SR_CONF_CURRENT_LIMIT:
			*data = scaled_double(cfg->set.curr, 1000);
			break;
		case SR_CONF_REGULATION:
			*data = g_variant_new_string(cfg->dev.output_cc ? "CC" :
						     cfg->dev.output_cv ? "CV" : "");
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

/**
 *  Helper function for const-casting a[]
 */
static int _std_str_idx(GVariant *data, char const * const a[], unsigned int n)
{
	return std_str_idx(data, (char const **)a, n);
}

/**
 *  libsigrok sr_dev_driver::config_set() api function
 */
static int peaktech_config_set(uint32_t key, GVariant *data,
			       struct sr_dev_inst const *sdi,
			       struct sr_channel_group const *cg)
{
	struct peaktech_device	*devc = sdi ? sdi->priv : NULL;

	if (!devc)
		return SR_ERR_ARG;

	if (!cg) {
		/* handle global configuration options */
		int		ival;
		bool		bval;

		switch (key) {
		case SR_CONF_CHANNEL_CONFIG:
			ival = _std_str_idx(data, ARRAY_AND_SIZE(CHANNEL_MODES));
			if (ival < 0)
				return SR_ERR_ARG;

			if (devc->model->num_chan == 1)
				/* 1-channel only models support only the
				 * current mode */
				return SR_ERR_ARG;

			if ((unsigned int)ival != devc->chan_mode || !devc->acq_running)
				devc->dirty |= BIT(PEAKTECH_DIRTY_CHAN_MODE);

			devc->chan_mode = ival;
			break;

		case SR_CONF_ENABLED:
			bval = g_variant_get_boolean(data);

			if (bval != devc->output_ena || !devc->acq_running)
				devc->dirty |= BIT(PEAKTECH_DIRTY_OUTPUT);

			devc->output_ena = bval;
			break;

		default:
			return SR_ERR_NA;
		}
	} else {
		/* handle channel group configuration options */
		struct peaktech_chan_config		*cfg = get_chan_config(cg);
		double					dval;

		switch (key) {
		case SR_CONF_VOLTAGE_TARGET:
			dval = g_variant_get_double(data);

			/* check limits */
			if (dval < CHAN_PARM[PEAKTECH_CHAN_CTRL_VOLT].min ||
			    dval > CHAN_PARM[PEAKTECH_CHAN_CTRL_VOLT].max)
				return SR_ERR_ARG;

			if (dval != cfg->set.curr || !devc->acq_running)
				cfg->dirty |= BIT(PEAKTECH_DIRTY_VOLT);

			cfg->set.volt = dval * 100;
			break;

		case SR_CONF_CURRENT_LIMIT:
			dval = g_variant_get_double(data);

			/* check limits */
			if (dval < CHAN_PARM[PEAKTECH_CHAN_CTRL_CURR].min ||
			    dval > CHAN_PARM[PEAKTECH_CHAN_CTRL_CURR].max)
				return SR_ERR_ARG;

			if (dval != cfg->set.curr || !devc->acq_running)
				cfg->dirty |= BIT(PEAKTECH_DIRTY_CURR);

			cfg->set.curr = dval * 1000;
			break;

		default:
			return SR_ERR_NA;
		}
	}

	/* when acquisition is not running, apply the values to the device */
	if (!devc->acq_running)
		return pt_setup_apply(devc);

	return SR_OK;
}

/**
 *  libsigrok sr_dev_driver::config_list() api function
 */
static int peaktech_config_list(uint32_t key, GVariant **data,
				struct sr_dev_inst const *sdi,
				struct sr_channel_group const *cg)
{
	struct peaktech_device	*devc = sdi ? sdi->priv : NULL;

	if (!cg) {
		/* handle global configuration options */
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, SCANOPTS, DRVOPTS, DEVOPTS);

		case SR_CONF_CHANNEL_CONFIG:
			if (!devc)
				return SR_ERR_ARG;

			if (devc->model->num_chan == 1)
				*data = g_variant_new_strv(&CHANNEL_MODES[PEAKTECH_CHAN_MODE_INDEPEDENT], 1);
			else
				*data = g_variant_new_strv(ARRAY_AND_SIZE(CHANNEL_MODES));
			break;

		default:
			return SR_ERR_NA;
		}
	} else {
		/* handle channel group configuration options */
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(DEVOPTS_CG));
			break;

		case SR_CONF_CURRENT_LIMIT:
		case SR_CONF_VOLTAGE_TARGET: {
			struct peaktech_chan_parm const	*parm;

			if (!devc)
				return SR_ERR_ARG;

			parm = &CHAN_PARM[(key == SR_CONF_CURRENT_LIMIT ?
					   PEAKTECH_CHAN_CTRL_CURR :
					   PEAKTECH_CHAN_CTRL_VOLT)];

			*data = std_gvar_min_max_step(parm->min, parm->max,
						      parm->step);

			break;
		}

		case SR_CONF_REGULATION:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(REGULATION_MODES));
			break;

		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

/**
 *  Helper function to prepare the asynch sending of setup requests
 */
static void _peaktech_prepare_send_setup(struct peaktech_device *devc,
				      char const *op)
{
	sr_dbg("sending async %s request", op);

	devc->send_len = sizeof devc->send_buf.setup;
	devc->send_pos = 0;
}

static void _peaktech_prepare_send_inquiry(struct peaktech_device *devc)
{
	sr_dbg("sending async INQUIRY request");

	devc->send_len = sizeof devc->send_buf.inquiry;
	devc->send_pos = 0;
}

/**
 *  Checks the "dirty" flags and prepares an asynchronous setup-request for
 *  the first found one.  The corresponding "dirty" flag will be set in the
 *  "cleanup" flag which is evaluated later when the confirmation is received.
 *
 *  When no "dirty" flag exists, an asynchronous INQUIRY request will be
 *  prepared.
 */
static void peaktech_send_next(struct peaktech_device *devc)
{
	enum peaktech_model		model = devc->model->model;

	/* clear the cleanup marker; they will be filled below */
	devc->cleanup = 0;
	devc->state = PEAKTECH_STATE_EXPECT_CONFIRM;

	for (size_t i = 0; i < ARRAY_SIZE(devc->config); ++i)
		devc->config[i].cleanup = 0;

	if (devc->dirty & BIT(PEAKTECH_DIRTY_CHAN_MODE)) {
		devc->send_buf.setup = pt_proto_chan_mode_req(model, devc->chan_mode);

		_peaktech_prepare_send_setup(devc, "CHAN_MODE");
		devc->cleanup = BIT(PEAKTECH_DIRTY_CHAN_MODE);

		goto out;
	}

	if (devc->dirty & BIT(PEAKTECH_DIRTY_OUTPUT)) {
		devc->send_buf.setup = pt_proto_output_en_req(model, devc->output_ena);

		_peaktech_prepare_send_setup(devc, "OUTPUT_SET");
		devc->cleanup = BIT(PEAKTECH_DIRTY_OUTPUT);

		goto out;
	}

	for (size_t i = 0; i < ARRAY_SIZE(devc->config); ++i) {
		struct peaktech_chan_config	*cfg = &devc->config[i];
		char				op[64];

		if (cfg->dirty & BIT(PEAKTECH_DIRTY_VOLT)) {
			devc->send_buf.setup = pt_proto_volt_set_req(model, i, cfg->set.volt);

			sprintf(op, "VOLT_SET@%zd", i);
			_peaktech_prepare_send_setup(devc, op);
			cfg->cleanup = BIT(PEAKTECH_DIRTY_VOLT);

			goto out;
		}

		if (cfg->dirty & BIT(PEAKTECH_DIRTY_CURR)) {
			devc->send_buf.setup = pt_proto_curr_set_req(model, i, cfg->set.curr);

			sprintf(op, "CURR_SET@%zd", i);
			_peaktech_prepare_send_setup(devc, op);
			cfg->cleanup = BIT(PEAKTECH_DIRTY_CURR);

			goto out;
		}
	}

	/* prepare an INQUIRY request */
	{
		devc->send_buf.inquiry = pt_proto_inquire_req(model);

		_peaktech_prepare_send_inquiry(devc);
		devc->state = PEAKTECH_STATE_EXPECT_INQUIRY;
	}

out:
	return;
}

static gboolean peaktech_next_state(struct peaktech_device *devc);

/**
 *  Callback for G_IO_OUT when device is in PEAKTECH_STATE_SEND state.
 *
 *  HACK: this function should be a real callback function for G_IO_OUT
 *  events.  But switching or reconfiguring event sources is not supported and
 *  this function is executed directly instead of.
 */
static gboolean peaktech_send_data_cb(int _fd, int revents, void *devc_)
{
	struct peaktech_device		*devc = devc_;
	struct sr_dev_inst		*sdi = devc ? devc->sdi : NULL;

	(void)_fd;
	g_assert(sdi);

	/* prepares the async sending by creating a request in
	 * devc->send_buf[] when state has been just entered */
	if (devc->send_len == devc->send_pos) {
		devc->send_pos = 0;
		devc->send_len = 0;

		g_assert_cmpint(devc->state, ==, PEAKTECH_STATE_SEND);

		peaktech_send_next(devc);

		g_assert_cmpuint(devc->send_len, !=, devc->send_pos);
	}

	/* peaktech_send_next() must have been called and this function
	 * changes the state */
	g_assert_cmpint(devc->state, !=, PEAKTECH_STATE_SEND);

	/* we call this function directly and expect that data is sent */
	g_assert(revents == G_IO_OUT);

	if (revents == G_IO_OUT) {
		int			len;
		void const		*ptr = &devc->send_buf.raw[devc->send_pos];
		size_t			cnt = devc->send_len - devc->send_pos;

		/* HACK: this should be nonblocking; but send_cb is called
		 * directly because sources can not be reonfigured.  Serial
		 * messages are small enough so that sending them should not
		 * block. */
		len = serial_write_blocking(sdi->conn, ptr, cnt,
					    serial_timeout(sdi->conn, cnt));
		if (len < 0) {
			sr_err("failed to send %zu data to device", cnt);
			return FALSE;
		}

		devc->send_pos += len;

		/* blocking send should send all bytes so that we can move to
		 * the next state below */
		g_assert_cmpuint(devc->send_pos, ==, devc->send_len);
	}

	/* when all data have been sent, go to the next state */
	if (devc->send_len == devc->send_pos)
		return peaktech_next_state(devc);

	return TRUE;
}

enum recv_result {
	RECV_INCOMPLETE,
	RECV_ERR,
	RECV_OK
};

/**
 *  Asynchronously receive data.
 *
 *  Function reads data in devc->recv_buf[] at the devc->recv_pos position.
 *  When expected number of bytes (max_len) have been read, CRC will be
 *  checked an RECV_OK returned.
 *
 *  When there are still data to read (max_len not reached), RECV_INCOMPLETE
 *  will be returned.
 */
static enum recv_result peaktech_recv(struct peaktech_device *devc,
				      int revent, size_t max_len)
{
	struct sr_dev_inst const	*sdi = devc->sdi;
	int				len;
	size_t				send_len;

	g_assert(max_len <= sizeof devc->recv_buf);
	g_assert(max_len > devc->recv_pos);

	if (revent == G_IO_ERR || revent == 0) {
		sr_warn("recv timed out or errored (event %d)", revent);
		return RECV_ERR;
	}

	send_len = max_len - devc->recv_pos;

	len = serial_read_nonblocking(sdi->conn,
				      &devc->recv_buf.raw[devc->recv_pos],
				      send_len);
	if (len < 0) {
		sr_warn("failed to read data: %d", len);
		return RECV_ERR;
	}

	devc->recv_pos += len;

	g_assert((size_t)len <= send_len);
	g_assert(devc->recv_pos <= sizeof devc->recv_buf);

	if (devc->recv_pos < max_len)
		return RECV_INCOMPLETE;

	if (!peaktech_607x_proto_crc_check(&devc->recv_buf, max_len)) {
		sr_warn("crc error");
		return RECV_ERR;
	}

	return RECV_OK;
}

/**
 *  Callback when device expects an INQUIRY response.
 *
 *  Function will generate a frame report by calling pt_inquiry_report() and
 *  enters PEAKTECH_STATE_SEND state when full response has been read.
 */
static gboolean peaktech_recv_inquiry_cb(int _fd, int revent, void *devc_)
{
	struct peaktech_device		*devc = devc_;

	(void)_fd;
	g_assert(devc);

	switch (peaktech_recv(devc, revent, devc->model->reply_size)) {
	case RECV_ERR:
		devc->state = PEAKTECH_STATE_ERR;
		break;

	case RECV_INCOMPLETE:
		return TRUE;

	case RECV_OK:
		pt_inquiry_parse(devc, &devc->recv_buf);
		pt_inquiry_report(devc);
		devc->state = PEAKTECH_STATE_SEND;
		break;
	}

	return peaktech_next_state(devc);
}

/**
 *  Callback when device expects a confirmation of a setup request.
 *
 *  When a confirmation has been read completely, the corresponding 'dirty'
 *  flags will be cleared and device enters PEAKTECH_STATE_SEND state.
 */
static gboolean peaktech_recv_confirm_cb(int _fd, int revent, void *devc_)
{
	struct peaktech_device		*devc = devc_;

	(void)_fd;
	g_assert(devc);
	g_assert(devc->send_len > 0);

	switch (peaktech_recv(devc, revent, devc->send_len)) {
	case RECV_ERR:
		devc->state = PEAKTECH_STATE_ERR;
		break;

	case RECV_INCOMPLETE:
		return TRUE;

	case RECV_OK:
#if 0					/* TODO: not supported in C99 */
		_Static_assert(sizeof devc->recv_buf >= sizeof devc->send_buf,
			       "bad recv/send buf layout");
#endif
		g_assert(devc->send_len == devc->recv_pos);

		/* size of read data is always lower than sent one; we can use
		 * 'devc->send_len' directly without extra checks. */
		if (memcmp(&devc->recv_buf, &devc->send_buf, devc->send_len) != 0) {
			sr_warn("mismatch in confirmed data");
			devc->state = PEAKTECH_STATE_ERR;
			break;
		}

		devc->dirty &= ~devc->cleanup;
		for (size_t i = 0; i < ARRAY_SIZE(devc->config); ++i)
			devc->config[i].dirty &= ~devc->config[i].cleanup;

		devc->state = PEAKTECH_STATE_SEND;
		break;;
	}

	return peaktech_next_state(devc);
}

/**
 *  Enters the next state.
 *
 *  It will add _peaktech_next_state_0() as an idle callback in the most
 *  cases.
 */
static gboolean peaktech_next_state(struct peaktech_device *devc)
{
	struct sr_dev_inst		*sdi = devc ? devc->sdi : NULL;

	g_assert(devc);

	if (devc->state == PEAKTECH_STATE_ERR)
		serial_flush(sdi->conn);

	switch (devc->state) {
	case PEAKTECH_STATE_INIT:
	case PEAKTECH_STATE_ERR:
		devc->recv_pos = 0;
		devc->state = PEAKTECH_STATE_SEND;
		/* fallthrough */

	case PEAKTECH_STATE_SEND:
		devc->send_len = 0;
		devc->send_pos = 0;
		/* HACK: it would be better to use this really as an
		 * asynchronous callback but switching event direction
		 * (G_IO_IN <-> G_IO_OUT) or whole source is not supported.
		 *
		 * Execute the callback directly. */
		devc->cb = NULL;
		return peaktech_send_data_cb(0, G_IO_OUT, devc);

	case PEAKTECH_STATE_EXPECT_CONFIRM:
		devc->recv_pos = 0;
		devc->cb = peaktech_recv_confirm_cb;
		break;

	case PEAKTECH_STATE_EXPECT_INQUIRY:
		devc->recv_pos = 0;
		devc->cb = peaktech_recv_inquiry_cb;
		break;
	}

	/* keep the actual source */
	return TRUE;
}

static gboolean peaktech_global_cb(int fd, int revent, void *devc_)
{
	struct peaktech_device		*devc = devc_;

	return devc->cb(fd, revent, devc);
}

/**
 *  libsigrok sr_dev_driver::dev_acquisition_start() api function
 */
static int peaktech_dev_acquisition_start(struct sr_dev_inst const *sdi)
{
	struct peaktech_device	*devc = sdi ? sdi->priv : NULL;

	if (!devc)
		return SR_ERR_ARG;

	devc->acq_running = true;
	devc->state = PEAKTECH_STATE_INIT;

	peaktech_next_state(devc);
	serial_source_add(sdi->session, sdi->conn, G_IO_IN, 100,
			  peaktech_global_cb, devc);

	std_session_send_df_header(sdi);

	return 0;
}

/**
 *  libsigrok sr_dev_driver::dev_acquisition_stop() api function
 */
static int peaktech_dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct peaktech_device	*devc = sdi ? sdi->priv : NULL;

	if (!devc)
		return SR_ERR_ARG;

	serial_source_remove(sdi->session, sdi->conn);

	return 0;
}

/**
 *  Parses generic options and open the serial device.
 *
 *  It returns optionally a model given by the 'force_detect' option.
 */
static struct sr_serial_dev_inst *
peaktech_serial_open(GSList *options, struct peaktech_model_desc const **model)
{
	char const			*serialcomm = SERIALCOMM;
	char const			*conn = NULL;
	char const			*model_str = NULL;
	struct sr_serial_dev_inst	*serial;

	for (GSList *l = options; l; l = l->next) {
		struct sr_config const	*src = l->data;

		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_FORCE_DETECT:
			model_str = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (!conn)
		return NULL;

	*model = NULL;
	if (model_str) {
		for (size_t i = 0; i < ARRAY_SIZE(MODELS); ++i) {
			if (strcmp(MODELS[i].name, model_str) == 0) {
				*model = &MODELS[i];
				break;
			}
		}

		if (!*model) {
			sr_err("unsupported model %s", model_str);
			return NULL;
		}
	}

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	return serial;
}

/**
 *  Tries to autodetect the model by sending model specific inquiries.
 */
static struct peaktech_model_desc const *
peaktech_scan_model(struct sr_serial_dev_inst *serial)
{
	for (size_t i = 0; i < ARRAY_SIZE(MODELS); ++i) {
		union pt_proto_inquire_reply		reply;
		int						rc;

		rc = pt_serial_send_inquiry(&MODELS[i], serial, &reply);
		if (rc == SR_OK)
			return &MODELS[i];

		sr_dbg("testing for model '%s' failed: %d", MODELS[i].name, rc);
	}

	return NULL;
}

/**
 *  Initializes common peaktech_report attributes
 */
static void pt_report_init_common(struct peaktech_device *devc,
				  struct peaktech_report *report)
{
	report->analog = (struct sr_datafeed_analog) {
		.data		= report->data,
		.num_samples	= 1,
		.encoding	= &report->encoding,
		.meaning	= &report->meaning,
		.spec		= &report->spec,
	};

	report->packet = (struct sr_datafeed_packet) {
		.type		= SR_DF_ANALOG,
		.payload	= &report->analog,
	};
}

/**
 *  Initializes "current" specific attributes in peaktech_report
 */
static void pt_report_init_curr(struct peaktech_device *devc)
{
	struct peaktech_report		*report = &devc->report_curr;

	report->meaning = (struct sr_analog_meaning) {
		.mq		= SR_MQ_CURRENT,
		.unit		= SR_UNIT_AMPERE,
		.mqflags	= SR_MQFLAG_DC,
		.channels	= devc->ch_curr,
	};

	report->spec = (struct sr_analog_spec) {
		.spec_digits	= 3,
	};

	report->encoding = (struct sr_analog_encoding) {
		.unitsize		= sizeof report->data[0],
		.is_float		= false,
		.is_bigendian		= IS_BIGENDIAN,
		.digits			= 3,
		.is_digits_decimal	= true,
		.scale			= {
			.p		= 1,
			.q		= 1000,
		},
		.offset			= {
			.p		= 0,
			.q		= 1,
		},
	};

	pt_report_init_common(devc, report);
}

/**
 *  Initializes "voltage" specific attributes in peaktech_report
 */
static void pt_report_init_volt(struct peaktech_device *devc)
{
	struct peaktech_report		*report = &devc->report_volt;

	report->meaning = (struct sr_analog_meaning) {
		.mq		= SR_MQ_VOLTAGE,
		.unit		= SR_UNIT_VOLT,
		.mqflags	= SR_MQFLAG_DC,
		.channels	= devc->ch_volt,
	};

	report->spec = (struct sr_analog_spec) {
		.spec_digits	= 2,
	};

	report->encoding = (struct sr_analog_encoding) {
		.unitsize		= sizeof report->data[0],
		.is_float		= false,
		.is_bigendian		= IS_BIGENDIAN,
		.digits			= 2,
		.is_digits_decimal	= true,
		.scale			= {
			.p		= 1,
			.q		= 100,
		},
		.offset			= {
			.p		= 0,
			.q		= 1,
		},
	};

	pt_report_init_common(devc, report);
}

/**
 *  libsigrok sr_dev_driver::scan() api function
 *
 *  It opens the serial device, detects the model and creates a
 *  peaktech_device object.
 */
static GSList *peaktech_scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst			*sdi;
	struct peaktech_device			*devc = NULL;
	struct sr_serial_dev_inst		*serial;
	struct peaktech_model_desc const	*model;

	serial = peaktech_serial_open(options, &model);
	if (!serial)
		return NULL;

	if (!model)
		model = peaktech_scan_model(serial);

	if (!model)
		goto out;

	sdi = g_malloc(sizeof *sdi);
	*sdi = (struct sr_dev_inst) {
		.status		= SR_ST_INACTIVE,
		.vendor		= g_strdup("PeakTech"),
		.model		= g_strdup(model->name),
		.inst_type	= SR_INST_SERIAL,
		.conn		= serial,
	};

	devc = g_malloc0(sizeof *devc);
	devc->sdi = sdi;
	devc->model = model;

	for (size_t i = 0; i < model->num_chan; ++i) {
		char			tmp[64];
		struct sr_channel	*ch;
		struct sr_channel_group	*cg;
		/* API requires that 'priv' is allocated */
		struct peaktech_chan_config **cfg_ref = g_malloc(sizeof *cfg_ref);

		sprintf(tmp, "CH%zd", i + 1);

		*cfg_ref = &devc->config[i];
		cg = sr_channel_group_new(sdi, tmp, cfg_ref);

		sprintf(tmp, "V%zd", i + 1);
		ch = sr_channel_new(sdi, i * 2, SR_CHANNEL_ANALOG, TRUE, tmp);
		cg->channels  = g_slist_append(cg->channels, ch);
		devc->ch_volt = g_slist_append(devc->ch_volt, ch);

		sprintf(tmp, "I%zd", i + 1);
		ch = sr_channel_new(sdi, i * 2 + 1, SR_CHANNEL_ANALOG, TRUE, tmp);
		cg->channels  = g_slist_append(cg->channels, ch);
		devc->ch_curr = g_slist_append(devc->ch_curr, ch);
	}

	pt_report_init_curr(devc);
	pt_report_init_volt(devc);

	sdi->priv = devc;

out:
	serial_close(serial);

	if (!devc)
		return NULL;

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

/**
 *  Destroys inner attributes of a peaktech_device object.
 *
 *  Called indirectly through peaktech_dev_clear() which is an
 *  sr_dev_driver::dev_clear api function.
 */
static void peaktech_destroy(void *devc_)
{
	struct peaktech_device			*devc = devc_;

	g_slist_free(devc->ch_curr);
	g_slist_free(devc->ch_volt);
}

/**
 *  libsigrok sr_dev_driver::dev_clear() api function
 */
static int peaktech_dev_clear(struct sr_dev_driver const *di)
{
	return std_dev_clear_with_callback(di, peaktech_destroy);
}

static struct sr_dev_driver		peaktech_607x_driver_info = {
	.name			= "peaktech-607x",
	.longname		= "PeakTech 6070/6075",
	.api_version		= 1,
	.init			= std_init,
	.cleanup		= std_cleanup,
	.scan			= peaktech_scan,
	.dev_list		= std_dev_list,
	.dev_clear		= peaktech_dev_clear,
	.config_get		= peaktech_config_get,
	.config_set		= peaktech_config_set,
	.config_list		= peaktech_config_list,
	.dev_open		= std_serial_dev_open,
	.dev_close		= std_serial_dev_close,
	.dev_acquisition_start	= peaktech_dev_acquisition_start,
	.dev_acquisition_stop	= peaktech_dev_acquisition_stop,
};
SR_REGISTER_DEV_DRIVER(peaktech_607x_driver_info);
