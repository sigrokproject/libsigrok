/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Analog Devices Inc.
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

#include "libm2k.h"
#include <libm2k/m2k.hpp>
#include <libm2k/contextbuilder.hpp>
#include <libm2k/digital/m2kdigital.hpp>
#include <libm2k/analog/m2kanalogin.hpp>
#include <libm2k/m2khardwaretrigger.hpp>
#include <libsigrok/libsigrok.h>
#include <string.h>

extern "C" {

libm2k::digital::M2kDigital *getDigital(struct M2k *m2k)
{
	libm2k::context::M2k *ctx = (libm2k::context::M2k *) m2k;
	return ctx->getDigital();
}

libm2k::analog::M2kAnalogIn *getAnalogIn(struct M2k *m2k)
{
	libm2k::context::M2k *ctx = (libm2k::context::M2k *) m2k;
	return ctx->getAnalogIn();
}

libm2k::M2kHardwareTrigger *getTrigger(struct M2k *m2k)
{
	libm2k::context::M2k *ctx = (libm2k::context::M2k *) m2k;
	return ctx->getDigital()->getTrigger();
}

/* Context */
M2k *sr_libm2k_context_open(const char *uri)
{
	libm2k::context::M2k *ctx;
	if (strlen(uri) == 0) {
		ctx = libm2k::context::m2kOpen();
	} else {
		ctx = libm2k::context::m2kOpen(uri);
	}
	return (M2k *) ctx;
}

int sr_libm2k_context_close(struct M2k **m2k)
{
	if (*m2k == nullptr) {
		return 0;
	}
	auto ctx = (libm2k::context::M2k *) *m2k;

	libm2k::context::contextClose(ctx, false);
	*m2k = nullptr;
	return 0;
}

void sr_libm2k_context_adc_calibrate(struct M2k *m2k)
{
	libm2k::context::M2k *ctx = (libm2k::context::M2k *) m2k;
	ctx->calibrateADC();
}

int sr_libm2k_context_get_all(struct CONTEXT_INFO ***info)
{
	auto ctxs = libm2k::context::getContextsInfo();

	struct CONTEXT_INFO **ctxs_info = (struct CONTEXT_INFO **) malloc(
		ctxs.size() * sizeof(struct CONTEXT_INFO *));
	for (unsigned int i = 0; i < ctxs.size(); ++i) {
		ctxs_info[i] = (struct CONTEXT_INFO *) malloc(sizeof(struct CONTEXT_INFO));
		ctxs_info[i]->id_vendor = ctxs[i]->id_vendor.c_str();
		ctxs_info[i]->id_product = ctxs[i]->id_product.c_str();
		ctxs_info[i]->manufacturer = ctxs[i]->manufacturer.c_str();
		ctxs_info[i]->product = ctxs[i]->product.c_str();
		ctxs_info[i]->serial = ctxs[i]->serial.c_str();
		ctxs_info[i]->uri = ctxs[i]->uri.c_str();
	}
	*info = ctxs_info;
	return ctxs.size();
}

int sr_libm2k_has_mixed_signal(struct M2k *m2k)
{
	libm2k::context::M2k *ctx = (libm2k::context::M2k *) m2k;
	return ctx->hasMixedSignal();
}

void sr_libm2k_mixed_signal_acquisition_start(struct M2k *m2k, unsigned int nb_samples)
{
	libm2k::context::M2k *ctx = (libm2k::context::M2k *) m2k;
	ctx->startMixedSignalAcquisition(nb_samples);
}

void sr_libm2k_mixed_signal_acquisition_stop(struct M2k *m2k)
{
	libm2k::context::M2k *ctx = (libm2k::context::M2k *) m2k;
	ctx->stopMixedSignalAcquisition();
}

/* Analog */
double sr_libm2k_analog_samplerate_get(struct M2k *m2k)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);
	return analogIn->getSampleRate();
}

double sr_libm2k_analog_samplerate_set(struct M2k *m2k, double samplerate)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);
	return analogIn->setSampleRate(samplerate);
}

int sr_libm2k_analog_oversampling_ratio_get(struct M2k *m2k)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);
	return analogIn->getOversamplingRatio();
}

void sr_libm2k_analog_oversampling_ratio_set(struct M2k *m2k, int oversampling)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);
	analogIn->setOversamplingRatio(oversampling);
}

enum M2K_RANGE sr_libm2k_analog_range_get(struct M2k *m2k, unsigned int channel)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);
	return static_cast<M2K_RANGE>(analogIn->getRange(
		static_cast<libm2k::analog::ANALOG_IN_CHANNEL>(channel)));
}

void sr_libm2k_analog_range_set(struct M2k *m2k, unsigned int channel, enum M2K_RANGE range)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);
	analogIn->setRange(static_cast<libm2k::analog::ANALOG_IN_CHANNEL>(channel),
			   static_cast<libm2k::analog::M2K_RANGE>(range));
}

void sr_libm2k_analog_acquisition_start(struct M2k *m2k, unsigned int buffer_size)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);
	analogIn->startAcquisition(buffer_size);
}

float **sr_libm2k_analog_samples_get(struct M2k *m2k, uint64_t nb_samples)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);

	const double *data = analogIn->getSamplesInterleaved(nb_samples);
	float **samples = new float *[2];
	samples[0] = new float[nb_samples];
	samples[1] = new float[nb_samples];
	for (unsigned int i = 0, j = 0; i < nb_samples; i++, j += 2) {
		samples[0][i] = (float) data[j];
		samples[1][i] = (float) data[j + 1];
	}
	return samples;
}

void sr_libm2k_analog_acquisition_cancel(struct M2k *m2k)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);
	analogIn->cancelAcquisition();
}

void sr_libm2k_analog_acquisition_stop(struct M2k *m2k)
{
	libm2k::analog::M2kAnalogIn *analogIn = getAnalogIn(m2k);
	analogIn->stopAcquisition();
}

/* Analog trigger */
enum ANALOG_TRIGGER_SOURCE sr_libm2k_analog_trigger_source_get(struct M2k *m2k)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	return static_cast<ANALOG_TRIGGER_SOURCE>(trigger->getAnalogSource());
}

void sr_libm2k_analog_trigger_source_set(struct M2k *m2k, enum ANALOG_TRIGGER_SOURCE source)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	trigger->setAnalogSource(static_cast<libm2k::M2K_TRIGGER_SOURCE_ANALOG>(source));
}

enum ANALOG_TRIGGER_MODE sr_libm2k_analog_trigger_mode_get(struct M2k *m2k, unsigned int chnIdx)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	return static_cast<ANALOG_TRIGGER_MODE>(trigger->getAnalogMode(chnIdx));
}

void sr_libm2k_analog_trigger_mode_set(struct M2k *m2k, unsigned int chnIdx,
				       enum ANALOG_TRIGGER_MODE mode)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	trigger->setAnalogMode(chnIdx, static_cast<libm2k::M2K_TRIGGER_MODE>(mode));
}

enum ANALOG_TRIGGER_CONDITION sr_libm2k_analog_trigger_condition_get(struct M2k *m2k, unsigned int chnIdx)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	return static_cast<ANALOG_TRIGGER_CONDITION>(trigger->getAnalogCondition(chnIdx));
}

void sr_libm2k_analog_trigger_condition_set(struct M2k *m2k, unsigned int chnIdx,
					    enum ANALOG_TRIGGER_CONDITION condition)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	trigger->setAnalogCondition(chnIdx, static_cast<libm2k::M2K_TRIGGER_CONDITION_ANALOG>(condition));
}

float sr_libm2k_analog_trigger_level_get(struct M2k *m2k, unsigned int chnIdx)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	return static_cast<float>(trigger->getAnalogLevel(chnIdx));
}

void sr_libm2k_analog_trigger_level_set(struct M2k *m2k, unsigned int chnIdx, float level)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	trigger->setAnalogLevel(chnIdx, static_cast<double>(level));
}

int sr_libm2k_analog_trigger_delay_get(struct M2k *m2k)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	return trigger->getAnalogDelay();
}

void sr_libm2k_analog_trigger_delay_set(struct M2k *m2k, int delay)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	trigger->setAnalogDelay(delay);
}

/* Digital */
double sr_libm2k_digital_samplerate_get(struct M2k *m2k)
{
	libm2k::digital::M2kDigital *digital = getDigital(m2k);
	return digital->getSampleRateIn();
}

double sr_libm2k_digital_samplerate_set(struct M2k *m2k, double samplerate)
{
	libm2k::digital::M2kDigital *digital = getDigital(m2k);
	return digital->setSampleRateIn(samplerate);
}

void sr_libm2k_digital_acquisition_start(struct M2k *m2k, unsigned int buffer_size)
{
	libm2k::digital::M2kDigital *digital = getDigital(m2k);
	digital->startAcquisition(buffer_size);
}

uint32_t *sr_libm2k_digital_samples_get(struct M2k *m2k, uint64_t nb_samples)
{
	libm2k::digital::M2kDigital *digital = getDigital(m2k);
	return (uint32_t *) digital->getSamplesP(nb_samples);
}

void sr_libm2k_digital_acquisition_cancel(struct M2k *m2k)
{
	libm2k::digital::M2kDigital *digital = getDigital(m2k);
	digital->cancelAcquisition();
}

void sr_libm2k_digital_acquisition_stop(struct M2k *m2k)
{
	libm2k::digital::M2kDigital *digital = getDigital(m2k);
	digital->stopAcquisition();
}

/* Digital trigger */
void sr_libm2k_digital_trigger_source_set(struct M2k *m2k, enum DIGITAL_TRIGGER_SOURCE source)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	trigger->setDigitalSource(static_cast<libm2k::M2K_TRIGGER_SOURCE_DIGITAL>(source));
}

enum M2K_TRIGGER_CONDITION_DIGITAL sr_libm2k_digital_trigger_condition_get(struct M2k *m2k, unsigned int chnIdx)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	return static_cast<M2K_TRIGGER_CONDITION_DIGITAL>(trigger->getDigitalCondition(chnIdx));
}

void sr_libm2k_digital_trigger_condition_set(struct M2k *m2k, unsigned int chnIdx, uint32_t cond)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	libm2k::M2K_TRIGGER_CONDITION_DIGITAL condition;
	switch (cond) {
	case SR_TRIGGER_ZERO:
		condition = libm2k::LOW_LEVEL_DIGITAL;
		break;
	case SR_TRIGGER_ONE:
		condition = libm2k::HIGH_LEVEL_DIGITAL;
		break;
	case SR_TRIGGER_RISING:
		condition = libm2k::RISING_EDGE_DIGITAL;
		break;
	case SR_TRIGGER_FALLING:
		condition = libm2k::FALLING_EDGE_DIGITAL;
		break;
	case SR_TRIGGER_EDGE:
		condition = libm2k::ANY_EDGE_DIGITAL;
		break;
	case SR_NO_TRIGGER:
		condition = libm2k::NO_TRIGGER_DIGITAL;
		break;
	default:
		condition = libm2k::NO_TRIGGER_DIGITAL;
	}
	trigger->setDigitalCondition(chnIdx, condition);
}

int sr_libm2k_digital_trigger_delay_get(struct M2k *m2k)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	return trigger->getDigitalDelay();
}

void sr_libm2k_digital_trigger_delay_set(struct M2k *m2k, int delay)
{
	libm2k::M2kHardwareTrigger *trigger = getTrigger(m2k);
	trigger->setDigitalDelay(delay);
}

}
