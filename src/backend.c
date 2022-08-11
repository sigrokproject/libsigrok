/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Peter Stuge <peter@stuge.se>
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
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "minilzo/minilzo.h"

/** @cond PRIVATE */
#define LOG_PREFIX "backend"
/** @endcond */

/**
 * @mainpage libsigrok API
 *
 * @section sec_intro Introduction
 *
 * The <a href="http://sigrok.org">sigrok</a> project aims at creating a
 * portable, cross-platform, Free/Libre/Open-Source signal analysis software
 * suite that supports various device types (such as logic analyzers,
 * oscilloscopes, multimeters, and more).
 *
 * <a href="http://sigrok.org/wiki/Libsigrok">libsigrok</a> is a shared
 * library written in C which provides the basic API for talking to
 * <a href="http://sigrok.org/wiki/Supported_hardware">supported hardware</a>
 * and reading/writing the acquired data into various
 * <a href="http://sigrok.org/wiki/Input_output_formats">input/output
 * file formats</a>.
 *
 * @section sec_api API reference
 *
 * See the "Modules" page for an introduction to various libsigrok
 * related topics and the detailed API documentation of the respective
 * functions.
 *
 * You can also browse the API documentation by file, or review all
 * data structures.
 *
 * @section sec_mailinglists Mailing lists
 *
 * There is one mailing list for sigrok/libsigrok: <a href="https://lists.sourceforge.net/lists/listinfo/sigrok-devel">sigrok-devel</a>.
 *
 * @section sec_irc IRC
 *
 * You can find the sigrok developers in the
 * <a href="ircs://irc.libera.chat/#sigrok">\#sigrok</a>
 * IRC channel on Libera.Chat.
 *
 * @section sec_website Website
 *
 * <a href="http://sigrok.org/wiki/Libsigrok">sigrok.org/wiki/Libsigrok</a>
 */

/**
 * @file
 *
 * Initializing and shutting down libsigrok.
 */

/**
 * @defgroup grp_init Initialization
 *
 * Initializing and shutting down libsigrok.
 *
 * Before using any of the libsigrok functionality (except for
 * sr_log_loglevel_set()), sr_init() must be called to initialize the
 * library, which will return a struct sr_context when the initialization
 * was successful.
 *
 * When libsigrok functionality is no longer needed, sr_exit() should be
 * called, which will (among other things) free the struct sr_context.
 *
 * Example for a minimal program using libsigrok:
 *
 * @code{.c}
 *   #include <stdio.h>
 *   #include <libsigrok/libsigrok.h>
 *
 *   int main(int argc, char **argv)
 *   {
 *   	int ret;
 *   	struct sr_context *sr_ctx;
 *
 *   	if ((ret = sr_init(&sr_ctx)) != SR_OK) {
 *   		printf("Error initializing libsigrok (%s): %s.\n",
 *   		       sr_strerror_name(ret), sr_strerror(ret));
 *   		return 1;
 *   	}
 *
 *   	// Use libsigrok functions here...
 *
 *   	if ((ret = sr_exit(sr_ctx)) != SR_OK) {
 *   		printf("Error shutting down libsigrok (%s): %s.\n",
 *   		       sr_strerror_name(ret), sr_strerror(ret));
 *   		return 1;
 *   	}
 *
 *   	return 0;
 *   }
 * @endcode
 *
 * @{
 */

SR_API GSList *sr_buildinfo_libs_get(void)
{
	GSList *l = NULL, *m = NULL;
#if defined(HAVE_LIBUSB_1_0) && !defined(__FreeBSD__)
	const struct libusb_version *lv;
#endif

	m = g_slist_append(NULL, g_strdup("glib"));
	m = g_slist_append(m, g_strdup_printf("%d.%d.%d (rt: %d.%d.%d/%d:%d)",
		GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION,
		glib_major_version, glib_minor_version, glib_micro_version,
		glib_binary_age, glib_interface_age));
	l = g_slist_append(l, m);

	m = g_slist_append(NULL, g_strdup("zlib"));
	m = g_slist_append(m, g_strdup_printf("%s", CONF_ZLIB_VERSION));
	l = g_slist_append(l, m);

	m = g_slist_append(NULL, g_strdup("libzip"));
	m = g_slist_append(m, g_strdup_printf("%s", CONF_LIBZIP_VERSION));
	l = g_slist_append(l, m);

	m = g_slist_append(NULL, g_strdup("minilzo"));
	m = g_slist_append(m, g_strdup_printf("%s", lzo_version_string()));
	l = g_slist_append(l, m);

#ifdef HAVE_LIBSERIALPORT
	m = g_slist_append(NULL, g_strdup("libserialport"));
	m = g_slist_append(m, g_strdup_printf("%s/%s (rt: %s/%s)",
		SP_PACKAGE_VERSION_STRING, SP_LIB_VERSION_STRING,
		sp_get_package_version_string(), sp_get_lib_version_string()));
	l = g_slist_append(l, m);
#endif
#ifdef HAVE_LIBUSB_1_0
	m = g_slist_append(NULL, g_strdup("libusb-1.0"));
#ifdef __FreeBSD__
	m = g_slist_append(m, g_strdup_printf("%s", CONF_LIBUSB_1_0_VERSION));
#else
	lv = libusb_get_version();
	m = g_slist_append(m, g_strdup_printf("%d.%d.%d.%d%s API 0x%08x",
		lv->major, lv->minor, lv->micro, lv->nano, lv->rc,
#if defined(LIBUSB_API_VERSION)
		LIBUSB_API_VERSION
#elif defined(LIBUSBX_API_VERSION)
		LIBUSBX_API_VERSION
#endif
		));
#endif
	l = g_slist_append(l, m);
#endif
#ifdef HAVE_LIBHIDAPI
	m = g_slist_append(NULL, g_strdup("hidapi"));
	m = g_slist_append(m, g_strdup_printf("%s", CONF_LIBHIDAPI_VERSION));
	l = g_slist_append(l, m);
#endif
#ifdef HAVE_LIBBLUEZ
	m = g_slist_append(NULL, g_strdup("bluez"));
	m = g_slist_append(m, g_strdup_printf("%s", CONF_LIBBLUEZ_VERSION));
	l = g_slist_append(l, m);
#endif
#ifdef HAVE_LIBFTDI
	m = g_slist_append(NULL, g_strdup("libftdi"));
	m = g_slist_append(m, g_strdup_printf("%s", CONF_LIBFTDI_VERSION));
	l = g_slist_append(l, m);
#endif
#ifdef HAVE_LIBGPIB
	m = g_slist_append(NULL, g_strdup("libgpib"));
	m = g_slist_append(m, g_strdup_printf("%s", CONF_LIBGPIB_VERSION));
	l = g_slist_append(l, m);
#endif
#ifdef HAVE_LIBREVISA
	m = g_slist_append(NULL, g_strdup("librevisa"));
	m = g_slist_append(m, g_strdup_printf("%s", CONF_LIBREVISA_VERSION));
	l = g_slist_append(l, m);
#endif

	return l;
}

SR_API char *sr_buildinfo_host_get(void)
{
	return g_strdup_printf("%s, %s-endian", CONF_HOST,
#ifdef WORDS_BIGENDIAN
	"big"
#else
	"little"
#endif
	);
}

SR_API char *sr_buildinfo_scpi_backends_get(void)
{
	GString *s;
	char *str;

	s = g_string_sized_new(200);

	g_string_append_printf(s, "TCP, ");
#if HAVE_RPC
	g_string_append_printf(s, "RPC, ");
#endif
#ifdef HAVE_SERIAL_COMM
	g_string_append_printf(s, "serial, ");
#endif
#ifdef HAVE_LIBREVISA
	g_string_append_printf(s, "VISA, ");
#endif
#ifdef HAVE_LIBGPIB
	g_string_append_printf(s, "GPIB, ");
#endif
#ifdef HAVE_LIBUSB_1_0
	g_string_append_printf(s, "USBTMC, ");
#endif
	s->str[s->len - 2] = '\0';

	str = g_strdup(s->str);
	g_string_free(s, TRUE);

	return str;
}

static void print_versions(void)
{
	GString *s;
	GSList *l, *l_orig, *m;
	char *str;
	const char *lib, *version;

	sr_dbg("libsigrok %s/%s.",
		sr_package_version_string_get(), sr_lib_version_string_get());

	s = g_string_sized_new(200);
	g_string_append(s, "Libs: ");
	l_orig = sr_buildinfo_libs_get();
	for (l = l_orig; l; l = l->next) {
		m = l->data;
		lib = m->data;
		version = m->next->data;
		g_string_append_printf(s, "%s %s, ", lib, version);
		g_slist_free_full(m, g_free);
	}
	g_slist_free(l_orig);
	s->str[s->len - 2] = '.';
	s->str[s->len - 1] = '\0';
	sr_dbg("%s", s->str);
	g_string_free(s, TRUE);

	str = sr_buildinfo_host_get();
	sr_dbg("Host: %s.", str);
	g_free(str);

	str = sr_buildinfo_scpi_backends_get();
	sr_dbg("SCPI backends: %s.", str);
	g_free(str);
}

static void print_resourcepaths(void)
{
	GSList *l, *l_orig;

	sr_dbg("Firmware search paths:");
	l_orig = sr_resourcepaths_get(SR_RESOURCE_FIRMWARE);
	for (l = l_orig; l; l = l->next)
		sr_dbg(" - %s", (const char *)l->data);
	g_slist_free_full(l_orig, g_free);
}

/**
 * Sanity-check all libsigrok drivers.
 *
 * @param[in] ctx Pointer to a libsigrok context struct. Must not be NULL.
 *
 * @retval SR_OK All drivers are OK
 * @retval SR_ERR One or more drivers have issues.
 * @retval SR_ERR_ARG Invalid argument.
 */
static int sanity_check_all_drivers(const struct sr_context *ctx)
{
	int i, errors, ret = SR_OK;
	struct sr_dev_driver **drivers;
	const char *d;

	if (!ctx)
		return SR_ERR_ARG;

	sr_spew("Sanity-checking all drivers.");

	drivers = sr_driver_list(ctx);
	for (i = 0; drivers[i]; i++) {
		errors = 0;

		d = (drivers[i]->name) ? drivers[i]->name : "NULL";

		if (!drivers[i]->name) {
			sr_err("No name in driver %d ('%s').", i, d);
			errors++;
		}
		if (!drivers[i]->longname) {
			sr_err("No longname in driver %d ('%s').", i, d);
			errors++;
		}
		if (drivers[i]->api_version < 1) {
			sr_err("API version in driver %d ('%s') < 1.", i, d);
			errors++;
		}
		if (!drivers[i]->init) {
			sr_err("No init in driver %d ('%s').", i, d);
			errors++;
		}
		if (!drivers[i]->cleanup) {
			sr_err("No cleanup in driver %d ('%s').", i, d);
			errors++;
		}
		if (!drivers[i]->scan) {
			sr_err("No scan in driver %d ('%s').", i, d);
			errors++;
		}
		if (!drivers[i]->dev_list) {
			sr_err("No dev_list in driver %d ('%s').", i, d);
			errors++;
		}
		if (!drivers[i]->dev_clear) {
			sr_err("No dev_clear in driver %d ('%s').", i, d);
			errors++;
		}
		/* Note: config_get() is optional. */
		if (!drivers[i]->config_set) {
			sr_err("No config_set in driver %d ('%s').", i, d);
			errors++;
		}
		/* Note: config_channel_set() is optional. */
		/* Note: config_commit() is optional. */
		if (!drivers[i]->config_list) {
			sr_err("No config_list in driver %d ('%s').", i, d);
			errors++;
		}
		if (!drivers[i]->dev_open) {
			sr_err("No dev_open in driver %d ('%s').", i, d);
			errors++;
		}
		if (!drivers[i]->dev_close) {
			sr_err("No dev_close in driver %d ('%s').", i, d);
			errors++;
		}
		if (!drivers[i]->dev_acquisition_start) {
			sr_err("No dev_acquisition_start in driver %d ('%s').",
			       i, d);
			errors++;
		}
		if (!drivers[i]->dev_acquisition_stop) {
			sr_err("No dev_acquisition_stop in driver %d ('%s').",
			       i, d);
			errors++;
		}

		/* Note: 'priv' is allowed to be NULL. */

		if (errors == 0)
			continue;

		ret = SR_ERR;
	}

	return ret;
}

/**
 * Sanity-check all libsigrok input modules.
 *
 * @retval SR_OK All modules are OK
 * @retval SR_ERR One or more modules have issues.
 */
static int sanity_check_all_input_modules(void)
{
	int i, errors, ret = SR_OK;
	const struct sr_input_module **inputs;
	const char *d;

	sr_spew("Sanity-checking all input modules.");

	inputs = sr_input_list();
	for (i = 0; inputs[i]; i++) {
		errors = 0;

		d = (inputs[i]->id) ? inputs[i]->id : "NULL";

		if (!inputs[i]->id) {
			sr_err("No ID in module %d ('%s').", i, d);
			errors++;
		}
		if (!inputs[i]->name) {
			sr_err("No name in module %d ('%s').", i, d);
			errors++;
		}
		if (!inputs[i]->desc) {
			sr_err("No description in module %d ('%s').", i, d);
			errors++;
		}
		if (!inputs[i]->init) {
			sr_err("No init in module %d ('%s').", i, d);
			errors++;
		}
		if (!inputs[i]->receive) {
			sr_err("No receive in module %d ('%s').", i, d);
			errors++;
		}
		if (!inputs[i]->end) {
			sr_err("No end in module %d ('%s').", i, d);
			errors++;
		}

		if (errors == 0)
			continue;

		ret = SR_ERR;
	}

	return ret;
}

/**
 * Sanity-check all libsigrok output modules.
 *
 * @retval SR_OK All modules are OK
 * @retval SR_ERR One or more modules have issues.
 */
static int sanity_check_all_output_modules(void)
{
	int i, errors, ret = SR_OK;
	const struct sr_output_module **outputs;
	const char *d;

	sr_spew("Sanity-checking all output modules.");

	outputs = sr_output_list();
	for (i = 0; outputs[i]; i++) {
		errors = 0;

		d = (outputs[i]->id) ? outputs[i]->id : "NULL";

		if (!outputs[i]->id) {
			sr_err("No ID in module %d ('%s').", i, d);
			errors++;
		}
		if (!outputs[i]->name) {
			sr_err("No name in module %d ('%s').", i, d);
			errors++;
		}
		if (!outputs[i]->desc) {
			sr_err("No description in module '%s'.", d);
			errors++;
		}
		if (!outputs[i]->receive) {
			sr_err("No receive in module '%s'.", d);
			errors++;
		}

		if (errors == 0)
			continue;

		ret = SR_ERR;
	}

	return ret;
}

/**
 * Sanity-check all libsigrok transform modules.
 *
 * @retval SR_OK All modules are OK
 * @retval SR_ERR One or more modules have issues.
 */
static int sanity_check_all_transform_modules(void)
{
	int i, errors, ret = SR_OK;
	const struct sr_transform_module **transforms;
	const char *d;

	sr_spew("Sanity-checking all transform modules.");

	transforms = sr_transform_list();
	for (i = 0; transforms[i]; i++) {
		errors = 0;

		d = (transforms[i]->id) ? transforms[i]->id : "NULL";

		if (!transforms[i]->id) {
			sr_err("No ID in module %d ('%s').", i, d);
			errors++;
		}
		if (!transforms[i]->name) {
			sr_err("No name in module %d ('%s').", i, d);
			errors++;
		}
		if (!transforms[i]->desc) {
			sr_err("No description in module '%s'.", d);
			errors++;
		}
		/* Note: options() is optional. */
		/* Note: init() is optional. */
		if (!transforms[i]->receive) {
			sr_err("No receive in module '%s'.", d);
			errors++;
		}
		/* Note: cleanup() is optional. */

		if (errors == 0)
			continue;

		ret = SR_ERR;
	}

	return ret;
}

/**
 * Initialize libsigrok.
 *
 * This function must be called before any other libsigrok function.
 *
 * @param ctx Pointer to a libsigrok context struct pointer. Must not be NULL.
 *            This will be a pointer to a newly allocated libsigrok context
 *            object upon success, and is undefined upon errors.
 *
 * @return SR_OK upon success, a (negative) error code otherwise. Upon errors
 *         the 'ctx' pointer is undefined and should not be used. Upon success,
 *         the context will be free'd by sr_exit() as part of the libsigrok
 *         shutdown.
 *
 * @since 0.2.0
 */
SR_API int sr_init(struct sr_context **ctx)
{
	int ret = SR_ERR;
	struct sr_context *context;
#ifdef _WIN32
	WSADATA wsadata;
#endif

	print_versions();

	print_resourcepaths();

	if (!ctx) {
		sr_err("%s(): libsigrok context was NULL.", __func__);
		return SR_ERR;
	}

	context = g_malloc0(sizeof(struct sr_context));

	sr_drivers_init(context);

	if (sanity_check_all_drivers(context) < 0) {
		sr_err("Internal driver error(s), aborting.");
		goto done;
	}

	if (sanity_check_all_input_modules() < 0) {
		sr_err("Internal input module error(s), aborting.");
		goto done;
	}

	if (sanity_check_all_output_modules() < 0) {
		sr_err("Internal output module error(s), aborting.");
		goto done;
	}

	if (sanity_check_all_transform_modules() < 0) {
		sr_err("Internal transform module error(s), aborting.");
		goto done;
	}

#ifdef _WIN32
	if ((ret = WSAStartup(MAKEWORD(2, 2), &wsadata)) != 0) {
		sr_err("WSAStartup failed with error code %d.", ret);
		ret = SR_ERR;
		goto done;
	}
#endif

	if ((ret = lzo_init()) != LZO_E_OK) {
		sr_err("lzo_init() failed with return code %d.", ret);
		sr_err("This usually indicates a compiler bug. Recompile without");
		sr_err("optimizations, and enable '-DLZO_DEBUG' for diagnostics.");
		ret = SR_ERR;
		goto done;
	}

#ifdef HAVE_LIBUSB_1_0
	ret = libusb_init(&context->libusb_ctx);
	if (LIBUSB_SUCCESS != ret) {
		sr_err("libusb_init() returned %s.", libusb_error_name(ret));
		ret = SR_ERR;
		goto done;
	}
#endif
#ifdef HAVE_LIBHIDAPI
	/*
	 * According to <hidapi.h>, the hid_init() routine just returns
	 * zero or non-zero, and hid_error() appears to relate to calls
	 * for a specific device after hid_open(). Which means that there
	 * is no more detailled information available beyond success/fail
	 * at this point in time.
	 */
	if (hid_init() != 0) {
		sr_err("HIDAPI hid_init() failed.");
		ret = SR_ERR;
		goto done;
	}
#endif
	sr_resource_set_hooks(context, NULL, NULL, NULL, NULL);

	*ctx = context;
	context = NULL;
	ret = SR_OK;

done:
	g_free(context);
	return ret;
}

/**
 * Shutdown libsigrok.
 *
 * @param ctx Pointer to a libsigrok context struct. Must not be NULL.
 *
 * @retval SR_OK Success
 * @retval other Error code SR_ERR, ...
 *
 * @since 0.2.0
 */
SR_API int sr_exit(struct sr_context *ctx)
{
	if (!ctx) {
		sr_err("%s(): libsigrok context was NULL.", __func__);
		return SR_ERR;
	}

	sr_hw_cleanup_all(ctx);

#ifdef _WIN32
	WSACleanup();
#endif

#ifdef HAVE_LIBHIDAPI
	hid_exit();
#endif
#ifdef HAVE_LIBUSB_1_0
	libusb_exit(ctx->libusb_ctx);
#endif

	g_free(sr_driver_list(ctx));
	g_free(ctx);

	return SR_OK;
}

/** @} */
