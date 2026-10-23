/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production discovery transaction without attached hardware. */
#include <config.h>
#include <check.h>
#include <string.h>
#include "lib.h"

#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>

static struct {
	struct libusb_config_descriptor config;
	struct libusb_interface interface;
	struct libusb_interface_descriptor setting;
	struct libusb_endpoint_descriptor endpoints[3];
	int descriptor_result, open_result, claim_result, write_result, read_result;
	int write_size, read_size;
	int freed, opened, claimed, transfers, released, closed;
	unsigned char reply[512];
} fixture;

static int mock_descriptor(libusb_device *dev,
	struct libusb_config_descriptor **config)
{
	(void)dev;
	*config = &fixture.config;
	return fixture.descriptor_result;
}

static void mock_free(struct libusb_config_descriptor *config)
{
	ck_assert_ptr_eq(config, &fixture.config);
	fixture.freed++;
}

static int mock_open(libusb_device *dev, libusb_device_handle **handle)
{
	(void)dev;
	fixture.opened++;
	*handle = (libusb_device_handle *)&fixture;
	return fixture.open_result;
}

static int mock_claim(libusb_device_handle *handle, int interface)
{
	ck_assert_ptr_eq(handle, (libusb_device_handle *)&fixture);
	ck_assert_int_eq(interface, 0);
	fixture.claimed++;
	return fixture.claim_result;
}

static int mock_bulk(libusb_device_handle *handle, unsigned char endpoint,
	unsigned char *data, int length, int *actual, unsigned int timeout)
{
	unsigned char expected[512] = {0x0a, 0x10};

	ck_assert_ptr_eq(handle, (libusb_device_handle *)&fixture);
	ck_assert_int_eq(length, 512);
	ck_assert_uint_eq(timeout, 1000);
	fixture.transfers++;
	if (fixture.transfers == 1) {
		ck_assert_int_eq(endpoint, 0x02);
		ck_assert_int_eq(memcmp(data, expected, sizeof(expected)), 0);
		*actual = fixture.write_size;
		return fixture.write_result;
	}
	ck_assert_int_eq(fixture.transfers, 2);
	ck_assert_int_eq(endpoint, 0x82);
	memcpy(data, fixture.reply, sizeof(fixture.reply));
	*actual = fixture.read_size;
	return fixture.read_result;
}

static int mock_release(libusb_device_handle *handle, int interface)
{
	ck_assert_ptr_eq(handle, (libusb_device_handle *)&fixture);
	ck_assert_int_eq(interface, 0);
	fixture.released++;
	return 0;
}

static void mock_close(libusb_device_handle *handle)
{
	ck_assert_ptr_eq(handle, (libusb_device_handle *)&fixture);
	fixture.closed++;
}

#define libusb_get_active_config_descriptor mock_descriptor
#define libusb_free_config_descriptor mock_free
#define libusb_open mock_open
#define libusb_claim_interface mock_claim
#define libusb_bulk_transfer mock_bulk
#define libusb_release_interface mock_release
#define libusb_close mock_close
#include "../src/hardware/fnirsi-dla32/discovery.h"

static void setup(void)
{
	unsigned int i;
	const unsigned char addresses[] = {0x02, 0x82, 0x81};

	memset(&fixture, 0, sizeof(fixture));
	fixture.config.bNumInterfaces = 1;
	fixture.config.interface = &fixture.interface;
	fixture.interface.num_altsetting = 1;
	fixture.interface.altsetting = &fixture.setting;
	fixture.setting.bNumEndpoints = 3;
	fixture.setting.endpoint = fixture.endpoints;
	for (i = 0; i < 3; i++) {
		fixture.endpoints[i].bEndpointAddress = addresses[i];
		fixture.endpoints[i].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
	}
	fixture.write_size = fixture.read_size = 512;
	fixture.reply[0] = 0x0a;
	fixture.reply[1] = 0x02;
	fixture.reply[2] = 0x0d;
	memcpy(fixture.reply + 13, "DL32", 4);
	fixture.reply[18] = 0x0b;
}

START_TEST(test_valid_identity)
{
	fixture.read_size = _i ? 19 : 512;
	ck_assert_int_eq(dla_libusb_probe(NULL), LIBUSB_SUCCESS);
	ck_assert_int_eq(fixture.freed, 1);
	ck_assert_int_eq(fixture.transfers, 2);
	ck_assert_int_eq(fixture.released, 1);
	ck_assert_int_eq(fixture.closed, 1);
}
END_TEST

START_TEST(test_descriptor_rejected)
{
	int expected = LIBUSB_ERROR_NOT_FOUND;

	switch (_i) {
	case 0:
		fixture.descriptor_result = expected = LIBUSB_ERROR_NO_DEVICE;
		break;
	case 1: fixture.setting.bNumEndpoints = 2; break;
	case 2: fixture.endpoints[2].bmAttributes = LIBUSB_TRANSFER_TYPE_INTERRUPT; break;
	case 3: fixture.setting.bInterfaceNumber = 1; break;
	case 4: fixture.setting.bAlternateSetting = 1; break;
	case 5: fixture.endpoints[2].bEndpointAddress = 0x83; break;
	}
	ck_assert_int_eq(dla_libusb_probe(NULL), expected);
	ck_assert_int_eq(fixture.freed, _i ? 1 : 0);
	ck_assert_int_eq(fixture.opened, 0);
	ck_assert_int_eq(fixture.transfers, 0);
}
END_TEST

START_TEST(test_access_or_busy)
{
	if (_i)
		fixture.claim_result = LIBUSB_ERROR_BUSY;
	else
		fixture.open_result = LIBUSB_ERROR_ACCESS;
	ck_assert_int_eq(dla_libusb_probe(NULL),
		_i ? LIBUSB_ERROR_BUSY : LIBUSB_ERROR_ACCESS);
	ck_assert_int_eq(fixture.opened, 1);
	ck_assert_int_eq(fixture.claimed, _i ? 1 : 0);
	ck_assert_int_eq(fixture.transfers, 0);
	ck_assert_int_eq(fixture.released, 0);
	ck_assert_int_eq(fixture.closed, _i ? 1 : 0);
}
END_TEST

START_TEST(test_transaction_rejected)
{
	int expected = LIBUSB_ERROR_NOT_FOUND;

	switch (_i) {
	case 0: fixture.write_result = expected = LIBUSB_ERROR_TIMEOUT; break;
	case 1: fixture.write_size = 511; expected = LIBUSB_ERROR_IO; break;
	case 2: fixture.read_result = expected = LIBUSB_ERROR_TIMEOUT; break;
	case 3: fixture.read_size = 18; break;
	case 4: memcpy(fixture.reply + 13, "DL16", 4); break;
	case 5: fixture.reply[0] = 0; break;
	case 6: fixture.reply[1] = 0; break;
	case 7: fixture.reply[2] = 0; break;
	case 8: fixture.reply[18] = 0; break;
	}
	ck_assert_int_eq(dla_libusb_probe(NULL), expected);
	ck_assert_int_eq(fixture.transfers, _i < 2 ? 1 : 2);
	ck_assert_int_eq(fixture.released, 1);
	ck_assert_int_eq(fixture.closed, 1);
}
END_TEST
#endif

Suite *suite_fnirsi_dla32_discovery(void)
{
	Suite *s = suite_create("fnirsi-dla32-discovery");
#ifdef HAVE_LIBUSB_1_0
	TCase *tc = tcase_create("libusb-identity");
	tcase_add_checked_fixture(tc, setup, NULL);
	tcase_add_loop_test(tc, test_valid_identity, 0, 2);
	tcase_add_loop_test(tc, test_descriptor_rejected, 0, 6);
	tcase_add_loop_test(tc, test_access_or_busy, 0, 2);
	tcase_add_loop_test(tc, test_transaction_rejected, 0, 9);
	suite_add_tcase(s, tc);
#endif
	return s;
}
