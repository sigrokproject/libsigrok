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

#include "protocol.h"

extern SR_PRIV struct sr_dev_driver link_mso19_driver_info;
static struct sr_dev_driver *di = &link_mso19_driver_info;

SR_PRIV int mso_parse_serial(const char *iSerial, const char *iProduct,
    struct dev_context *devc)
{
	unsigned int u1, u2, u3, u4, u5, u6;

  iProduct = iProduct;
  /* FIXME: This code is in the original app, but I think its
   * used only for the GUI */
  /*	if (strstr(iProduct, "REV_02") || strstr(iProduct, "REV_03"))
      devc->num_sample_rates = 0x16;
      else
      devc->num_sample_rates = 0x10; */
  

	/* parse iSerial */
	if (iSerial[0] != '4' || sscanf(iSerial, "%5u%3u%3u%1u%1u%6u",
				&u1, &u2, &u3, &u4, &u5, &u6) != 6)
		return SR_ERR;
	devc->hwmodel = u4;
	devc->hwrev = u5;
	devc->serial = u6;
	devc->vbit = u1 / 10000;
	if (devc->vbit == 0)
		devc->vbit = 4.19195;
	devc->dac_offset = u2;
	if (devc->dac_offset == 0)
		devc->dac_offset = 0x1ff;
	devc->offset_range = u3;
	if (devc->offset_range == 0)
		devc->offset_range = 0x17d;

	/*
	 * FIXME: There is more code on the original software to handle
	 * bigger iSerial strings, but as I can't test on my device
	 * I will not implement it yet
	 */

	return SR_OK;
}

SR_PRIV int mso_send_control_message(struct sr_serial_dev_inst *serial,
    uint16_t payload[], int n)
{
	int i, w, ret, s = n * 2 + sizeof(mso_head) + sizeof(mso_foot);
	char *p, *buf;

	ret = SR_ERR;

	if (serial->fd < 0)
		goto ret;

	if (!(buf = g_try_malloc(s))) {
		sr_err("Failed to malloc message buffer.");
		ret = SR_ERR_MALLOC;
		goto ret;
	}

	p = buf;
	memcpy(p, mso_head, sizeof(mso_head));
	p += sizeof(mso_head);

	for (i = 0; i < n; i++) {
		*(uint16_t *) p = htons(payload[i]);
		p += 2;
	}
	memcpy(p, mso_foot, sizeof(mso_foot));

	w = 0;
	while (w < s) {
		ret = serial_write(serial, buf + w, s - w);
		if (ret < 0) {
			ret = SR_ERR;
			goto free;
		}
		w += ret;
	}
	ret = SR_OK;
free:
	g_free(buf);
ret:
	return ret;
}

SR_PRIV int mso_reset_adc(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint16_t ops[2];

	ops[0] = mso_trans(REG_CTL1, (devc->ctlbase1 | BIT_CTL1_RESETADC));
	ops[1] = mso_trans(REG_CTL1, devc->ctlbase1);
	devc->ctlbase1 |= BIT_CTL1_ADC_UNKNOWN4;

	sr_dbg("Requesting ADC reset.");
	return mso_send_control_message(devc->serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV void stop_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_datafeed_packet packet;
	struct dev_context *devc;

	devc = sdi->priv;
	sr_source_remove(devc->serial->fd);

	/* Terminate session */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);
}

SR_PRIV int mso_clkrate_out(struct sr_serial_dev_inst *serial, uint16_t val)
{
	uint16_t ops[] = {
		mso_trans(REG_CLKRATE1, (val >> 8) & 0xff),
		mso_trans(REG_CLKRATE2, val & 0xff),
	};

	sr_dbg("Setting clkrate word to 0x%x.", val);
	return mso_send_control_message(serial, ARRAY_AND_SIZE(ops));
}

SR_PRIV int mso_configure_rate(struct sr_dev_inst *sdi, uint32_t rate)
{
	struct dev_context *devc = sdi->priv;
	unsigned int i;
	int ret = SR_ERR;

	for (i = 0; i < ARRAY_SIZE(rate_map); i++) {
		if (rate_map[i].rate == rate) {
			devc->ctlbase2 = rate_map[i].slowmode;
			ret = mso_clkrate_out(sdi, rate_map[i].val);
			if (ret == SR_OK)
				devc->cur_rate = rate;
			return ret;
		}
	}
	return ret;
}





SR_PRIV int mso_check_trigger(struct sr_serial_dev_inst *serial, uint8_t *info)
{
	uint16_t ops[] = { mso_trans(REG_TRIGGER, 0) };
	int ret;

	sr_dbg("Requesting trigger state.");
	ret = mso_send_control_message(serial, ARRAY_AND_SIZE(ops));
	if (info == NULL || ret != SR_OK)
		return ret;


  uint8_t buf = 0;
	if (serial_read(serial, &buf, 1) != 1) /* FIXME: Need timeout */
		ret = SR_ERR;
	*info = buf;

	sr_dbg("Trigger state is: 0x%x.", *info);
	return ret;
}

SR_PRIV int mso_receive_data(int fd, int revents, void *cb_data)
{

	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;
	int num_channels, offset, i, j;
	unsigned char byte;

	drvc = di->priv;

	/* Find this device's devc struct by its fd. */
	devc = NULL;
	for (l = drvc->instances; l; l = l->next) {
		sdi = l->data;
		devc = sdi->priv;
		if (devc->serial->fd == fd)
			break;
		devc = NULL;
	}
	if (!devc)
		/* Shouldn't happen. */
		return TRUE;

	(void)revents;

	uint8_t in[1024];
	size_t s = serial_read(devc->serial, in, sizeof(in));
	if (s <= 0)
		return FALSE;
  
  /* No samples */
  if (devc->trigger_state != MSO_TRIGGER_DATAREADY) {
    devc->trigger_state = in[0];
    if (devc->trigger_state == MSO_TRIGGER_DATAREADY) {
      mso_read_buffer(sdi);
      devc->buffer_n = 0;
    } else {
      mso_check_trigger(devc->serial, NULL);
    }
    return FALSE;
  }

	/* the hardware always dumps 1024 samples, 24bits each */
	if (devc->buffer_n < 3072) {
		memcpy(devc->buffer + devc->buffer_n, in, s);
		devc->buffer_n += s;
	}
	if (devc->buffer_n < 3072)
		return FALSE;

	/* do the conversion */
	uint8_t logic_out[1024];
	double analog_out[1024];
	for (i = 0; i < 1024; i++) {
		/* FIXME: Need to do conversion to mV */
		analog_out[i] = (devc->buffer[i * 3] & 0x3f) |
			((devc->buffer[i * 3 + 1] & 0xf) << 6);
		logic_out[i] = ((devc->buffer[i * 3 + 1] & 0x30) >> 4) |
			((devc->buffer[i * 3 + 2] & 0x3f) << 2);
	}

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = 1024;
	logic.unitsize = 1;
	logic.data = logic_out;
	sr_session_send(cb_data, &packet);

	// Dont bother fixing this yet, keep it "old style"
	/*
	packet.type = SR_DF_ANALOG;
	packet.length = 1024;
	packet.unitsize = sizeof(double);
	packet.payload = analog_out;
	sr_session_send(ctx->session_dev_id, &packet);
	*/

	packet.type = SR_DF_END;
	sr_session_send(devc->session_dev_id, &packet);

}
