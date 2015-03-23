/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Kumar Abhishek <abhishek@theembeddedkitchen.net>
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

#ifndef BEAGLELOGIC_H_
#define BEAGLELOGIC_H_

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>

/* BeagleLogic device node name */
#define BEAGLELOGIC_DEV_NODE        "/dev/beaglelogic"
#define BEAGLELOGIC_SYSFS_ATTR(a)   "/sys/devices/virtual/misc/beaglelogic/"\
					__STRING(a)

/* Reproduced verbatim from beaglelogic.h in the kernel tree until the kernel
 * module hits the mainline. Contains the ABI, so DO NOT TOUCH this section */

/* ioctl calls that can be issued on /dev/beaglelogic */
#define IOCTL_BL_GET_VERSION        _IOR('k', 0x20, uint32_t)

#define IOCTL_BL_GET_SAMPLE_RATE    _IOR('k', 0x21, uint32_t)
#define IOCTL_BL_SET_SAMPLE_RATE    _IOW('k', 0x21, uint32_t)

#define IOCTL_BL_GET_SAMPLE_UNIT    _IOR('k', 0x22, uint32_t)
#define IOCTL_BL_SET_SAMPLE_UNIT    _IOW('k', 0x22, uint32_t)

#define IOCTL_BL_GET_TRIGGER_FLAGS  _IOR('k', 0x23, uint32_t)
#define IOCTL_BL_SET_TRIGGER_FLAGS  _IOW('k', 0x23, uint32_t)

#define IOCTL_BL_CACHE_INVALIDATE    _IO('k', 0x25)

#define IOCTL_BL_GET_BUFFER_SIZE    _IOR('k', 0x26, uint32_t)
#define IOCTL_BL_SET_BUFFER_SIZE    _IOW('k', 0x26, uint32_t)

#define IOCTL_BL_GET_BUFUNIT_SIZE   _IOR('k', 0x27, uint32_t)

#define IOCTL_BL_FILL_TEST_PATTERN   _IO('k', 0x28)

#define IOCTL_BL_START               _IO('k', 0x29)
#define IOCTL_BL_STOP                _IO('k', 0x2A)

/* Possible States of BeagleLogic */
enum beaglelogic_states {
	STATE_BL_DISABLED,	/* Powered off (at module start) */
	STATE_BL_INITIALIZED,	/* Powered on */
	STATE_BL_MEMALLOCD,	/* Buffers allocated */
	STATE_BL_ARMED,		/* All Buffers DMA-mapped and configuration done */
	STATE_BL_RUNNING,	/* Data being captured */
	STATE_BL_REQUEST_STOP,	/* Stop requested */
	STATE_BL_ERROR   	/* Buffer overrun */
};

/* Setting attributes */
enum beaglelogic_triggerflags {
	BL_TRIGGERFLAGS_ONESHOT = 0,
	BL_TRIGGERFLAGS_CONTINUOUS
};

/* Possible sample unit / formats */
enum beaglelogic_sampleunit {
	BL_SAMPLEUNIT_16_BITS = 0,
	BL_SAMPLEUNIT_8_BITS
};
/* END beaglelogic.h */

/* For all the functions below:
 * Parameters:
 * 	devc : Device context structure to operate on
 * Returns:
 * 	SR_OK or SR_ERR
 */

SR_PRIV int beaglelogic_open_nonblock(struct dev_context *devc);
SR_PRIV int beaglelogic_close(struct dev_context *devc);

SR_PRIV int beaglelogic_get_buffersize(struct dev_context *devc);
SR_PRIV int beaglelogic_set_buffersize(struct dev_context *devc);

SR_PRIV int beaglelogic_get_samplerate(struct dev_context *devc);
SR_PRIV int beaglelogic_set_samplerate(struct dev_context *devc);

SR_PRIV int beaglelogic_get_sampleunit(struct dev_context *devc);
SR_PRIV int beaglelogic_set_sampleunit(struct dev_context *devc);

SR_PRIV int beaglelogic_get_triggerflags(struct dev_context *devc);
SR_PRIV int beaglelogic_set_triggerflags(struct dev_context *devc);

/* Start and stop the capture operation */
SR_PRIV int beaglelogic_start(struct dev_context *devc);
SR_PRIV int beaglelogic_stop(struct dev_context *devc);

/* Get the last error size */
SR_PRIV int beaglelogic_getlasterror(struct dev_context *devc);

/* Gets the unit size of the capture buffer (usually 4 or 8 MB) */
SR_PRIV int beaglelogic_get_bufunitsize(struct dev_context *devc);

SR_PRIV int beaglelogic_mmap(struct dev_context *devc);
SR_PRIV int beaglelogic_munmap(struct dev_context *devc);

/* Sources */
SR_PRIV inline int beaglelogic_open_nonblock(struct dev_context *devc) {
	devc->fd = open(BEAGLELOGIC_DEV_NODE, O_RDONLY | O_NONBLOCK);
	return (devc->fd == -1 ? SR_ERR : SR_OK);
}

SR_PRIV inline int beaglelogic_close(struct dev_context *devc) {
	return close(devc->fd);
}

SR_PRIV inline int beaglelogic_get_buffersize(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_GET_BUFFER_SIZE, &devc->buffersize);
}

SR_PRIV inline int beaglelogic_set_buffersize(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_SET_BUFFER_SIZE, devc->buffersize);
}

/* This is treated differently as it gets a uint64_t while a uint32_t is read */
SR_PRIV inline int beaglelogic_get_samplerate(struct dev_context *devc) {
	uint32_t arg, err;
	err = ioctl(devc->fd, IOCTL_BL_GET_SAMPLE_RATE, &arg);
	devc->cur_samplerate = arg;
	return err;
}

SR_PRIV inline int beaglelogic_set_samplerate(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_SET_SAMPLE_RATE,
			(uint32_t)devc->cur_samplerate);
}

SR_PRIV inline int beaglelogic_get_sampleunit(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_GET_SAMPLE_UNIT, &devc->sampleunit);
}

SR_PRIV inline int beaglelogic_set_sampleunit(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_SET_SAMPLE_UNIT, devc->sampleunit);
}

SR_PRIV inline int beaglelogic_get_triggerflags(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_GET_TRIGGER_FLAGS, &devc->triggerflags);
}

SR_PRIV inline int beaglelogic_set_triggerflags(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_SET_TRIGGER_FLAGS, devc->triggerflags);
}

SR_PRIV int beaglelogic_getlasterror(struct dev_context *devc) {
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

SR_PRIV inline int beaglelogic_start(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_START);
}

SR_PRIV inline int beaglelogic_stop(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_STOP);
}

SR_PRIV int beaglelogic_get_bufunitsize(struct dev_context *devc) {
	return ioctl(devc->fd, IOCTL_BL_GET_BUFUNIT_SIZE, &devc->bufunitsize);
}

SR_PRIV int beaglelogic_mmap(struct dev_context *devc) {
	if (!devc->buffersize)
		beaglelogic_get_buffersize(devc);
	devc->sample_buf = mmap(NULL, devc->buffersize,
			PROT_READ, MAP_SHARED, devc->fd, 0);
	return (devc->sample_buf == MAP_FAILED ? -1 : SR_OK);
}

SR_PRIV int beaglelogic_munmap(struct dev_context *devc) {
	return munmap(devc->sample_buf, devc->buffersize);
}

#endif
