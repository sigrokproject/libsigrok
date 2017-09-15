/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014, 2017 Kumar Abhishek <abhishek@theembeddedkitchen.net>
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
#include "beaglelogic.h"

static int beaglelogic_open_nonblock(struct dev_context *devc) {
	devc->fd = open(BEAGLELOGIC_DEV_NODE, O_RDONLY | O_NONBLOCK);
	return (devc->fd == -1 ? SR_ERR : SR_OK);
}

static int beaglelogic_close(struct dev_context *devc) {
	return close(devc->fd);
}

static int beaglelogic_get_buffersize(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_GET_BUFFER_SIZE, &devc->buffersize);
}

static int beaglelogic_set_buffersize(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_SET_BUFFER_SIZE, devc->buffersize);
}

/* This is treated differently as it gets a uint64_t while a uint32_t is read */
static int beaglelogic_get_samplerate(struct dev_context *devc) {
	uint32_t arg, err;
	err = ioctl(devc->fd, IOCTL_BL_GET_SAMPLE_RATE, &arg);
	devc->cur_samplerate = arg;
	return err;
}

static int beaglelogic_set_samplerate(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_SET_SAMPLE_RATE,
			(uint32_t)devc->cur_samplerate);
}

static int beaglelogic_get_sampleunit(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_GET_SAMPLE_UNIT, &devc->sampleunit);
}

static int beaglelogic_set_sampleunit(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_SET_SAMPLE_UNIT, devc->sampleunit);
}

static int beaglelogic_get_triggerflags(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_GET_TRIGGER_FLAGS, &devc->triggerflags);
}

static int beaglelogic_set_triggerflags(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_SET_TRIGGER_FLAGS, devc->triggerflags);
}

static int beaglelogic_get_lasterror(struct dev_context *devc) {
	int fd;
	char buf[16];
	int ret;

	if ((fd = open(BEAGLELOGIC_SYSFS_ATTR(lasterror), O_RDONLY)) == -1)
		return SR_ERR;

	if ((ret = read(fd, buf, 16)) < 0)
		return SR_ERR;

	close(fd);
	devc->last_error = strtoul(buf, NULL, 10);

	return SR_OK;
}

static int beaglelogic_start(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_START);
}

static int beaglelogic_stop(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_STOP);
}

static int beaglelogic_get_bufunitsize(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_GET_BUFUNIT_SIZE, &devc->bufunitsize);
}

static int beaglelogic_set_bufunitsize(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_SET_BUFUNIT_SIZE, devc->bufunitsize);
}

static int beaglelogic_mmap(struct dev_context *devc) {
	if (!devc->buffersize)
		beaglelogic_get_buffersize(devc);
	devc->sample_buf = mmap(NULL, devc->buffersize,
			PROT_READ, MAP_SHARED, devc->fd, 0);
	return (devc->sample_buf == MAP_FAILED ? -1 : SR_OK);
}

static int beaglelogic_munmap(struct dev_context *devc) {
	return munmap(devc->sample_buf, devc->buffersize);
}

SR_PRIV const struct beaglelogic_ops beaglelogic_native_ops = {
	.open = beaglelogic_open_nonblock,
	.close = beaglelogic_close,
	.get_buffersize = beaglelogic_get_buffersize,
	.set_buffersize = beaglelogic_set_buffersize,
	.get_samplerate = beaglelogic_get_samplerate,
	.set_samplerate = beaglelogic_set_samplerate,
	.get_sampleunit = beaglelogic_get_sampleunit,
	.set_sampleunit = beaglelogic_set_sampleunit,
	.get_triggerflags = beaglelogic_get_triggerflags,
	.set_triggerflags = beaglelogic_set_triggerflags,
	.start = beaglelogic_start,
	.stop = beaglelogic_stop,
	.get_lasterror = beaglelogic_get_lasterror,
	.get_bufunitsize = beaglelogic_get_bufunitsize,
	.set_bufunitsize = beaglelogic_set_bufunitsize,
	.mmap = beaglelogic_mmap,
	.munmap = beaglelogic_munmap,
};
