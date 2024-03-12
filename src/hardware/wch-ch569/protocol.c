/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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
#include <glib.h>
#include <glib/gstdio.h>
#include <pthread.h>
#include "protocol.h"

#pragma pack(push, 1)

#define MB (1024 * 1024)

struct version_info {
	uint8_t major;
	uint8_t minor;
};

struct cmd_start_acquisition {
	uint8_t flags;
	uint8_t sample_delay_h;
	uint8_t sample_delay_l;
};

#pragma pack(pop)

#define USB_TIMEOUT 100
unsigned long transfer_count = 0;

unsigned long long int recvLength = 0, sbytes_prev = 0;
static struct timeval t1, t2;

static int command_get_fw_version(libusb_device_handle *devhdl,
				  struct version_info *vi)
{
	int ret;

	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, CMD_GET_FW_VERSION, 0x0000, 0x0000,
		(unsigned char *)vi, sizeof(struct version_info), USB_TIMEOUT);

	if (ret < 0) {
		sr_err("Unable to get version info: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int command_get_revid_version(struct sr_dev_inst *sdi, uint8_t *revid)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	libusb_device_handle *devhdl = usb->devhdl;
	int ret;

	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, CMD_GET_REVID_VERSION, 0x0000, 0x0000,
		revid, 1, USB_TIMEOUT);

	if (ret < 0) {
		sr_err("Unable to get REVID: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int command_start_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	uint64_t samplerate;
	struct cmd_start_acquisition cmd;
	int delay, ret, j;
	int calvalue_1 = 48;
	int calvalue_2 = 30;

	devc = sdi->priv;
	usb = sdi->conn;
	samplerate = devc->cur_samplerate;

	// cal rate
	sr_dbg("current devices pid: %04x", devc->profile->pid);
	if (devc->profile->usb_speed == LIBUSB_SPEED_SUPER) {
		if (samplerate < SR_MHZ(1)) {
			calvalue_1 = WCH_LOGICV8;
			calvalue_2 = WCH_LOGICV8L;
		} else {
			calvalue_1 = WCH_LOGICV16;
			calvalue_2 = WCH_LOGICV16L;
		}
	} else {
		calvalue_1 = WCH_LOGICV8;
		calvalue_2 = WCH_LOGICV8L;
	}

	/* Compute the sample rate. */
	if (devc->sample_wide && samplerate > MAX_16BIT_SAMPLE_RATE) {
		sr_err("Unable to sample at %" PRIu64 "Hz "
		       "when collecting 16-bit samples.", samplerate);
		return SR_ERR;
	}

	sr_dbg("samplerate value : %ld. calvale: %d", samplerate, calvalue_1);
	delay = 0;
	cmd.flags = cmd.sample_delay_h = cmd.sample_delay_l = 0;
	if ((SR_MHZ(calvalue_1) % samplerate) == 0) {
		cmd.flags = CMD_START_FLAGS_CLK_48MHZ;
		delay = SR_MHZ(calvalue_1) / samplerate - 1;
		if (delay > MAX_SAMPLE_DELAY)
			delay = 0;
	}

	if (delay == 0 && (SR_MHZ(calvalue_2) % samplerate) == 0) {
		cmd.flags = CMD_START_FLAGS_CLK_30MHZ;
		delay = SR_MHZ(calvalue_2) / samplerate - 1;
	}

	sr_dbg("GPIF delay = %d, clocksource = %sMHz.", delay,
		(cmd.flags & CMD_START_FLAGS_CLK_48MHZ) ? "calvalue" : "30");

	if (delay < 0 || delay > MAX_SAMPLE_DELAY) {
		sr_err("Unable to sample at %" PRIu64 "Hz.", samplerate);
		return SR_ERR;
	}

	sr_dbg("delay value : %d .", delay);
	cmd.sample_delay_h = (delay >> 8) & 0xff;
	cmd.sample_delay_l = delay & 0xff;

	/* Select the sampling width. */
	cmd.flags |= devc->sample_wide ? CMD_START_FLAGS_SAMPLE_16BIT :
		CMD_START_FLAGS_SAMPLE_8BIT;

	/* Enable CTL2 clock. */
	cmd.flags |= (g_slist_length(devc->enabled_analog_channels) > 0) ? CMD_START_FLAGS_CLK_CTL2 : 0;

	/* Send the control message. */
	ret = libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_OUT, CMD_START, 0x0000, 0x0000,
			(unsigned char *)&cmd, sizeof(cmd), USB_TIMEOUT);	
	sr_dbg("control cmd : %#2x %#2x %#2x .", cmd.sample_delay_h, cmd.sample_delay_l, cmd.flags);
	if (ret < 0) {
		sr_err("Unable to send start command: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int command_stop_acquisition(const struct sr_dev_inst *sdi) 
{
	struct sr_usb_dev_inst *usb;
	int ret;
	uint8_t ack;

	usb = sdi->conn;
	ret = libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_OUT, CMD_STOP, 0x0000, 0x0000,
			&ack, 0, USB_TIMEOUT);
	
	if (ret < 0) {
		sr_err("Failed to send stop command: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int wch_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di)
{
	libusb_device **devlist;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct version_info vi;
	int ret = SR_ERR, i, device_count;
	uint8_t revid;
	char connection_id[64];

	drvc = di->context;
	devc = sdi->priv;
	usb = sdi->conn;

	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != devc->profile->vid
		    || des.idProduct != devc->profile->pid)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) ||
				(sdi->status == SR_ST_INACTIVE)) {
			/*
			 * Check device by its physical USB bus/port address.
			 */
			if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
				continue;

			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		if (libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER)) {
			if (libusb_kernel_driver_active(usb->devhdl, USB_INTERFACE) == 1) {
				if ((ret = libusb_detach_kernel_driver(usb->devhdl, USB_INTERFACE)) < 0) {
					sr_err("Failed to detach kernel driver: %s.",
						libusb_error_name(ret));
					ret = SR_ERR;
					break;
				}
			}
		}

		ret = command_get_fw_version(usb->devhdl, &vi);
		if (ret != SR_OK) {
			sr_err("Failed to get firmware version.");
			break;
		}

		ret = command_get_revid_version(sdi, &revid);
		if (ret != SR_OK) {
			sr_err("Failed to get REVID.");
			break;
		}

		/*
		 * Changes in major version mean incompatible/API changes, so
		 * bail out if we encounter an incompatible version.
		 * Different minor versions are OK, they should be compatible.
		 */
		if (vi.major != WCH_REQUIRED_VERSION_MAJOR) {
			sr_err("Expected firmware version %d.x, "
			       "got %d.%d.", WCH_REQUIRED_VERSION_MAJOR,
			       vi.major, vi.minor);
			break;
		}

		sr_info("Opened device on %d.%d (logical) / %s (physical), "
			"interface %d, firmware %d.%d.",
			usb->bus, usb->address, connection_id,
			USB_INTERFACE, vi.major, vi.minor);

		ret = SR_OK;

		break;
	}

	libusb_free_device_list(devlist, 1);

	return ret;
}

// init 
SR_PRIV struct dev_context *wch_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->profile = NULL;
	devc->fw_updated = 0;
	devc->cur_samplerate = 0;
	devc->limit_frames = 1;
	devc->limit_samples = 0;
	devc->capture_ratio = 0;
	devc->sample_wide = FALSE;
	devc->num_frames = 0;
	devc->stl = NULL;

	return devc;
}

SR_PRIV void wch_abort_acquisition(struct sr_dev_inst *sdi, struct dev_context *devc)
{
	int i;

	devc->acq_aborted = TRUE;

	for (i = devc->num_transfers - 1; i >= 0; i--) {
		sr_info("num_transfers cancel:%d, i:%d.\n", devc->num_transfers, i);
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
	
	if (command_stop_acquisition(sdi)) {
		sr_info("stop devices failed.");
	}
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	std_session_send_df_end(sdi);

	usb_source_remove(sdi->session, devc->ctx);

	devc->num_transfers = 0;
	g_free(devc->transfers);

	/* Free the deinterlace buffers if we had them. */
	if (g_slist_length(devc->enabled_analog_channels) > 0) {
		g_free(devc->logic_buffer);
		g_free(devc->analog_buffer);
	}

	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}

	transfer_count = 0;
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	unsigned int i;

	sdi = transfer->user_data;
	devc = sdi->priv;

	g_free(transfer->buffer);
	transfer->buffer = NULL;
	libusb_free_transfer(transfer);

	for (i = 0; i < devc->num_transfers; i++) {
		if (devc->transfers[i] == transfer) {
			devc->transfers[i] = NULL;
			break;
		}
	}

	devc->submitted_transfers--;
	if (devc->submitted_transfers == 0) {
		finish_acquisition(sdi);
	}
}

static void resubmit_transfer(struct libusb_transfer *transfer)
{
	int ret;

	if ((ret = libusb_submit_transfer(transfer)) == LIBUSB_SUCCESS)
		return;

	sr_err("%s: %s", __func__, libusb_error_name(ret));
	free_transfer(transfer);
}

static void mso_send_data_proc(struct sr_dev_inst *sdi,
	uint8_t *data, size_t length, size_t sample_width)
{
	sr_info("Enter the API [mso_send_data_proc]");

	size_t i;
	struct dev_context *devc;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	(void)sample_width;

	devc = sdi->priv;

	length /= 2;

	/* Send the logic */
	for (i = 0; i < length; i++) {
		devc->logic_buffer[i] = data[i * 2];
		/* Rescale to -10V - +10V from 0-255. */
		devc->analog_buffer[i] = (data[i * 2 + 1] - 128.0f) / 12.8f;
	};

	const struct sr_datafeed_logic logic = {
		.length = length,
		.unitsize = 1,
		.data = devc->logic_buffer
	};

	const struct sr_datafeed_packet logic_packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic
	};

	sr_session_send(sdi, &logic_packet);

	sr_analog_init(&analog, &encoding, &meaning, &spec, 2);
	analog.meaning->channels = devc->enabled_analog_channels;
	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = 0 /* SR_MQFLAG_DC */;
	analog.num_samples = length;
	analog.data = devc->analog_buffer;

	const struct sr_datafeed_packet analog_packet = {
		.type = SR_DF_ANALOG,
		.payload = &analog
	};

	sr_session_send(sdi, &analog_packet);
}

// send the data
static void la_send_data_proc(struct sr_dev_inst *sdi,
	uint8_t *data, size_t length, size_t sample_width)
{
	sr_info("Enter the API [la_send_data_proc]");

	const struct sr_datafeed_logic logic = {
		.length = length,
		.unitsize = sample_width,
		.data = data
	};

	const struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic
	};

	sr_session_send(sdi, &packet);
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	int i = 0;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	gboolean packet_has_error = FALSE;
	unsigned int num_samples;
	int trigger_offset, cur_sample_count, unitsize, processed_samples;
	int pre_trigger_samples;

	int nwrite = 0;

	int delta_sec = 0;
	int delta_usec = 0;
	float speed_read = 0.0;

	sdi = transfer->user_data;
	devc = sdi->priv;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (devc->acq_aborted) {
		free_transfer(transfer);
		return;
	}

	transfer_count++;
	// sr_dbg("id:%d receive_transfer(): status %s received %d bytes.",
	// 	transfer_count, libusb_error_name(transfer->status), transfer->actual_length);
	recvLength += transfer->actual_length;

	if ((recvLength - sbytes_prev) >= (50 * MB)) {
		gettimeofday(&t2, NULL);
		delta_sec = t2.tv_sec - t1.tv_sec;
		delta_usec = t2.tv_usec - t1.tv_usec;
		if (delta_usec < 0) {
			delta_sec--;
			delta_usec = 1000000 + delta_usec;
		}
		speed_read = (float)(50) / (float)(delta_sec + (float)delta_usec / 1000000); 
		sr_info("TX 50 MB takes: %2d sec %2d us, up speed: %f MB/s\n",
			delta_sec, delta_usec, speed_read);
		gettimeofday(&t1, NULL);
		sbytes_prev = recvLength;
	}
	sr_info("capture total data: %lld", recvLength);

	/* Save incoming transfer before reusing the transfer struct. */
	unitsize = devc->sample_wide ? 2 : 1;
	cur_sample_count = transfer->actual_length / unitsize;
	processed_samples = 0;

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		wch_abort_acquisition(sdi, devc);
		free_transfer(transfer);
		return;
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though. */
		break;
	default:
		packet_has_error = TRUE;
		break;
	}

	if (transfer->actual_length == 0 || packet_has_error) {
		devc->empty_transfer_count++;
		if (devc->empty_transfer_count > MAX_EMPTY_TRANSFERS) {
			/*
			 * The WCH gave up. End the acquisition, the frontend
			 * will work out that the samplecount is short.
			 */
			wch_abort_acquisition(sdi, devc);
			free_transfer(transfer);
		} else {
			resubmit_transfer(transfer);
		}
		return;
	} else {
		devc->empty_transfer_count = 0;
	}
#if 1
check_trigger:
	if (devc->trigger_fired) {
		if (!devc->limit_samples || devc->sent_samples < devc->limit_samples) {
			/* Send the incoming transfer to the session bus. */
			num_samples = cur_sample_count - processed_samples;
			if (devc->limit_samples && devc->sent_samples + num_samples > devc->limit_samples)
				num_samples = devc->limit_samples - devc->sent_samples;

			devc->send_data_proc(sdi, (uint8_t *)transfer->buffer + processed_samples * unitsize,
				num_samples * unitsize, unitsize);
			devc->sent_samples += num_samples;
			processed_samples += num_samples;
		}
	} else {
		trigger_offset = soft_trigger_logic_check(devc->stl,
			transfer->buffer + processed_samples * unitsize,
			transfer->actual_length - processed_samples * unitsize,
			&pre_trigger_samples);
		if (trigger_offset > -1) {
			std_session_send_df_frame_begin(sdi);
			devc->sent_samples += pre_trigger_samples;
			num_samples = cur_sample_count - processed_samples - trigger_offset;
			if (devc->limit_samples &&
					devc->sent_samples + num_samples > devc->limit_samples)
				num_samples = devc->limit_samples - devc->sent_samples;

			devc->send_data_proc(sdi, (uint8_t *)transfer->buffer
					+ processed_samples * unitsize
					+ trigger_offset * unitsize,
					num_samples * unitsize, unitsize);
			devc->sent_samples += num_samples;
			processed_samples += trigger_offset + num_samples;

			devc->trigger_fired = TRUE;
		}
	}

	const int frame_ended = devc->limit_samples && (devc->sent_samples >= devc->limit_samples);
	const int final_frame = devc->limit_frames && (devc->num_frames >= (devc->limit_frames - 1));

	sr_info("frame_ended:%d final_frame:%d.\n", frame_ended, final_frame);

	if (frame_ended) {
		devc->num_frames++;
		devc->sent_samples = 0;
		devc->trigger_fired = FALSE;
		std_session_send_df_frame_end(sdi);

		/* There may be another trigger in the remaining data, go back and check for it */
		if (processed_samples < cur_sample_count) {
			/* Reset the trigger stage */
			if (devc->stl)
				devc->stl->cur_stage = 0;
			else {
				std_session_send_df_frame_begin(sdi);
				devc->trigger_fired = TRUE;
			}
			if (!final_frame)
				goto check_trigger;
		}
	}

	if (frame_ended && final_frame) {
		wch_abort_acquisition(sdi, devc);
		free_transfer(transfer);
	} else
		resubmit_transfer(transfer);
#endif
}

static int configure_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const GSList *l;
	int p;
	struct sr_channel *ch;
	uint32_t channel_mask = 0, num_analog = 0;

	devc = sdi->priv;

	g_slist_free(devc->enabled_analog_channels);
	devc->enabled_analog_channels = NULL;

	sr_info("sdi->channels: %d", sdi->channels);

	for (l = sdi->channels, p = 0; l; l = l->next, p++) {
		ch = l->data;
		if ((p <= NUM_CHANNELS) && (ch->type == SR_CHANNEL_ANALOG)
				&& (ch->enabled)) {
			num_analog++;
			devc->enabled_analog_channels =
			    g_slist_append(devc->enabled_analog_channels, ch);
		} else {
			channel_mask |= ch->enabled << p;
		}
	}

	/*
	 * Use wide sampling if either any of the LA channels 8..15 is enabled,
	 * and/or at least one analog channel is enabled.
	 */
	devc->sample_wide = channel_mask > 0xff || num_analog > 0;

	sr_info("devc->sample_wide:%08x", devc->sample_wide);

	return SR_OK;
}

static unsigned int to_bytes_per_ms(struct dev_context *devc)
{
	return devc->cur_samplerate / 1000;
}

static size_t get_buffer_size(struct dev_context *devc)
{
	size_t s;

	/*
	 * The buffer should be large enough to hold 10ms of data and
	 * a multiple of 512.
	 */
	if (devc->cur_samplerate > SR_MHZ(100))
		s = 10 * to_bytes_per_ms(devc);
	else 
		s = 15 * to_bytes_per_ms(devc);
#if 1
	// 2022-03-03 change the buffsize to transfer
	if (devc->profile->usb_speed == LIBUSB_SPEED_FULL) {
		return (s + 511ULL) & ~511ULL;
	} else if (devc->profile->usb_speed == LIBUSB_SPEED_SUPER) {
		// return (s + 1023ULL) & ~1023ULL;
		return (1024 * 1024 * 1.5);
	}
#else 
	// 2022-4-1 change the packet nums
	return (s + 511) & ~511;
#endif 
}

static unsigned int get_number_of_transfers(struct dev_context *devc)
{
	unsigned int n;

	sr_info("buffer size: %d", get_buffer_size(devc));

	// if (devc->cur_samplerate < SR_MHZ(100)) {
	// 	n = (600 * to_bytes_per_ms(devc) / get_buffer_size(devc));
	// } else if (devc->cur_samplerate < SR_MHZ(600)) {
	// 	n = (50 * to_bytes_per_ms(devc) / get_buffer_size(devc));
	// } 

	if (devc->profile->usb_speed == LIBUSB_SPEED_FULL) {
		n = (600 * to_bytes_per_ms(devc) / get_buffer_size(devc));
	} else if (devc->profile->usb_speed == LIBUSB_SPEED_SUPER) {
		n = 64;
	}

	sr_info("transfers num:%d.\n", n);

	if (n > NUM_SIMUL_TRANSFERS)
		return NUM_SIMUL_TRANSFERS;

	return n;
}

static unsigned int get_timeout(struct dev_context *devc)
{
	size_t total_size;
	unsigned int timeout;

	total_size = get_buffer_size(devc) *
			get_number_of_transfers(devc);
	sr_dbg("%s : the total size %ld.\n", __func__, total_size);
	timeout = total_size / to_bytes_per_ms(devc);

	// if (devc->profile->usb_speed == LIBUSB_SPEED_SUPER) 
		// return 500;
		// return timeout * 2;

	// if (devc->cur_samplerate > SR_MHZ(100))
	// 	return (timeout + timeout / 4) * 5;

	// if (devc->profile->pid == 0x453b) {
	// 	return (timeout + timeout / 4) * 3;
	// 	// return 0;
	// }

	return timeout + timeout / 3; /* Leave a headroom of 25% percent. */
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct timeval tv;
	struct drv_context *drvc;
	int completed = 0;

	(void)fd;
	(void)revents;

	drvc = (struct drv_context *)cb_data;

	tv.tv_sec = tv.tv_usec = 0;
	// libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	// 20220622 change the timeout and the events arriver api
	libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv, &completed);

	return TRUE;
}

static int start_transfers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct sr_trigger *trigger;
	struct libusb_transfer *transfer;
	unsigned int i, num_transfers;
	int timeout, ret;
	unsigned char *buf;
	size_t size;

	devc = sdi->priv;
	usb = sdi->conn;

	devc->sent_samples = 0;
	devc->acq_aborted = FALSE;
	devc->empty_transfer_count = 0;

	if ((trigger = sr_session_trigger_get(sdi->session))) {
		int pre_trigger_samples = 0;
		if (devc->limit_samples > 0)
			pre_trigger_samples = (devc->capture_ratio * devc->limit_samples) / 100;
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre_trigger_samples);
		if (!devc->stl)
			return SR_ERR_MALLOC;
		devc->trigger_fired = FALSE;
	} else {
		std_session_send_df_frame_begin(sdi);
		devc->trigger_fired = TRUE;
	}

	num_transfers = get_number_of_transfers(devc);
	size = get_buffer_size(devc);

	sr_dbg("the buffer size: %d.", size);
	devc->submitted_transfers = 0;

	devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * (num_transfers+1));
	if (!devc->transfers) {	
		sr_err("USB transfers malloc failed.");
		return SR_ERR_MALLOC;
	}

	sr_info("The transfer : %d.\n", num_transfers);
	timeout = get_timeout(devc);
	
	sr_dbg("timeout: %d.", timeout);
	devc->num_transfers = num_transfers;

	for (i = 0; i < num_transfers; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("USB transfer buffer malloc failed.");
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
				2 | LIBUSB_ENDPOINT_IN, buf, size,
				receive_transfer, (void *)sdi, timeout);
		sr_info("submitting transfer: %d", i);
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));

			libusb_free_transfer(transfer);
			g_free(buf);
			wch_abort_acquisition(sdi, devc);
			return SR_ERR;
		}
		devc->transfers[i] = transfer;
		devc->submitted_transfers++;
	}

	/*
	 * If this device has analog channels and at least one of them is
	 * enabled, use mso_send_data_proc() to properly handle the analog
	 * data. Otherwise use la_send_data_proc().
	 */
	if (g_slist_length(devc->enabled_analog_channels) > 0) {
		devc->send_data_proc = mso_send_data_proc;	
		sr_dbg("deal with the analog data.");
	} else {
		devc->send_data_proc = la_send_data_proc;
		sr_dbg("deal with the la data.");
	}
		
	std_session_send_df_header(sdi);

	return SR_OK;
}

SR_PRIV int wch_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct dev_context *devc;
	int timeout, ret;
	size_t size;

	di = sdi->driver;
	drvc = di->context;
	devc = sdi->priv;

	devc->ctx = drvc->sr_ctx;
	devc->num_frames = 0;
	devc->sent_samples = 0;
	devc->empty_transfer_count = 0;
	devc->acq_aborted = FALSE;

	if ((ret = command_stop_acquisition(sdi)) != SR_OK) {
		wch_abort_acquisition(sdi, devc);
		return ret;
	}

	g_usleep(500);

	if (configure_channels(sdi) != SR_OK) {
		sr_err("Failed to configure channels.");
		return SR_ERR;
	}

	timeout = get_timeout(devc);
	sr_dbg("wch_acquisition_start timeout: %d", timeout);
	usb_source_add(sdi->session, devc->ctx, timeout, receive_data, drvc);

	size = get_buffer_size(devc);
	/* Prepare for analog sampling. */
	if (g_slist_length(devc->enabled_analog_channels) > 0) {
		/* We need a buffer half the size of a transfer. */
		devc->logic_buffer = g_try_malloc(size / 2);
		devc->analog_buffer = g_try_malloc(
			sizeof(float) * size / 2);
	}

	if ((ret = command_start_acquisition(sdi)) != SR_OK) {
		wch_abort_acquisition(sdi, devc);
		return ret;
	}

	start_transfers(sdi);

	return SR_OK;
}

SR_PRIV int wch_acquisition_stop(const struct sr_dev_inst *sdi)
{ 
	recvLength = 0;
	command_stop_acquisition(sdi);
	wch_abort_acquisition(sdi, sdi->priv);

	return SR_OK;
}