/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Guido Trentalancia <guido@trentalancia.com>
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

/* Model descriptions for the Rohde&Schwarz / Hameg driver. */

/*
 * This is the basic dialect supported on the Hameg HMO series
 * and on the Rohde&Schwarz HMO and RTC1000 series.
 *
 * It doesn't support directly setting the sample rate, although
 * it supports setting the maximum sample rate.
 *
 * It supports setting a logic threshold for Logic (Pattern)
 * Trigger on digitized analog channels (custom level).
 *
 * It supports the Random Sampling (10x or 12.5x maximum sample
 * rate) and the Acquisition Mode settings. Note that the Random
 * Sampling feature might only be available on HMO2524 and HMO3000,
 * according to the latest available User Manual version.
 *
 * The system beep functionality is misteriously missing from HMO
 * Compact and HMO2524 User Manuals...
 */
static const char *rohde_schwarz_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		      = ":FORM UINT,8;:POD%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		      = ":TIM:SCAL?",
	[SCPI_CMD_SET_TIMEBASE]		      = ":TIM:SCAL %s",
	[SCPI_CMD_GET_HORIZONTAL_DIV]	      = ":TIM:DIV?",
	[SCPI_CMD_GET_COUPLING]		      = ":CHAN%d:COUP?",
	[SCPI_CMD_SET_COUPLING]		      = ":CHAN%d:COUP %s",
	[SCPI_CMD_GET_SAMPLE_RATE]	      = ":ACQ:SRAT?",
	[SCPI_CMD_GET_WAVEFORM_SAMPLE_RATE]   = ":ACQ:WRAT?",
	[SCPI_CMD_SET_WAVEFORM_SAMPLE_RATE]   = ":ACQ:WRAT %s",
	[SCPI_CMD_GET_RANDOM_SAMPLING]	      = ":ACQ:REAL?",	/* HMO2524 and HMO3000 series only ! */
	[SCPI_CMD_SET_RANDOM_SAMPLING]	      = ":ACQ:REAL %s",	/* HMO2524 and HMO3000 series only ! */
	[SCPI_CMD_GET_ACQUISITION_MODE]	      = ":ACQ:MODE?",
	[SCPI_CMD_SET_ACQUISITION_MODE]	      = ":ACQ:MODE %s",
	[SCPI_CMD_GET_ARITHMETICS_TYPE]	      = ":CHAN:ARIT?",   /* No index needed. Don't use ACQ:TYPE ! */
	[SCPI_CMD_SET_ARITHMETICS_TYPE]	      = ":CHAN:ARIT %s", /* No index needed. Don't use ACQ:TYPE ! */
	[SCPI_CMD_GET_INTERPOLATION_MODE]     = ":ACQ:INT?",
	[SCPI_CMD_SET_INTERPOLATION_MODE]     = ":ACQ:INT %s",
	[SCPI_CMD_GET_ANALOG_DATA]	      = ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL?",
	[SCPI_CMD_SET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL %s",
	[SCPI_CMD_GET_DIG_POD_STATE]	      = ":POD%d:STAT?",
	[SCPI_CMD_SET_DIG_POD_STATE]	      = ":POD%d:STAT %d",
	[SCPI_CMD_GET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR?",
	[SCPI_CMD_SET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR %s",
	[SCPI_CMD_GET_TRIGGER_SLOPE]	      = ":TRIG:A:EDGE:SLOP?",
	[SCPI_CMD_SET_TRIGGER_SLOPE]	      = ":TRIG:A:TYPE EDGE;:TRIG:A:EDGE:SLOP %s",
	[SCPI_CMD_GET_TRIGGER_PATTERN]	      = ":TRIG:A:PATT:SOUR?",
	[SCPI_CMD_SET_TRIGGER_PATTERN]	      = ":TRIG:A:TYPE LOGIC;" \
					        ":TRIG:A:PATT:FUNC AND;" \
					        ":TRIG:A:PATT:COND \"TRUE\";" \
					        ":TRIG:A:PATT:MODE OFF;" \
					        ":TRIG:A:PATT:SOUR \"%s\"",
	[SCPI_CMD_GET_HIGH_RESOLUTION]	      = ":ACQ:HRES?",
	[SCPI_CMD_SET_HIGH_RESOLUTION]	      = ":ACQ:HRES %s",
	[SCPI_CMD_GET_PEAK_DETECTION]	      = ":ACQ:PEAK?",
	[SCPI_CMD_SET_PEAK_DETECTION]	      = ":ACQ:PEAK %s",
	[SCPI_CMD_GET_DIG_CHAN_STATE]	      = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_CHAN_STATE]	      = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_VERTICAL_OFFSET]	      = ":CHAN%d:POS?",
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	      = ":TIM:POS?",
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	      = ":TIM:POS %s",
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT?",
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT %d",
	[SCPI_CMD_GET_PROBE_UNIT]	      = ":PROB%d:SET:ATT:UNIT?",
	[SCPI_CMD_GET_ANALOG_THRESHOLD]	      = ":CHAN%d:THR?",
	[SCPI_CMD_SET_ANALOG_THRESHOLD]	      = ":CHAN%d:THR %s",
	[SCPI_CMD_GET_DIG_POD_THRESHOLD]      = ":POD%d:THR?",
	[SCPI_CMD_SET_DIG_POD_THRESHOLD]      = ":POD%d:THR %s",
	[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD] = ":POD%d:THR:UDL%d?",
	[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD] = ":POD%d:THR:UDL%d %s",
	[SCPI_CMD_GET_BANDWIDTH_LIMIT]	      = ":CHAN%d:BAND?",
	[SCPI_CMD_SET_BANDWIDTH_LIMIT]	      = ":CHAN%d:BAND %s",
	[SCPI_CMD_GET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR?",
	[SCPI_CMD_SET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR:DEF \"%s\"",
	[SCPI_CMD_GET_FFT_SAMPLE_RATE]	      = ":CALC:MATH%d:FFT:SRAT?",
	[SCPI_CMD_SET_FFT_SAMPLE_RATE]	      = ":CALC:MATH%d:FFT:SRAT %s",
	[SCPI_CMD_GET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE?",
	[SCPI_CMD_SET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP?",
	[SCPI_CMD_SET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN?",
	[SCPI_CMD_SET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR %s",
	[SCPI_CMD_GET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:ADJ?",
	[SCPI_CMD_SET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:VAL %s",
	[SCPI_CMD_GET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO %d",
	[SCPI_CMD_GET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT %d",
	[SCPI_CMD_GET_FFT_DATA]		      = ":CALC:MATH%d:ARIT OFF;" \
					        ":CALC:MATH%d:FFT:MAGN:SCAL DBM;" \
					        ":CALC:MATH%d:SCAL 20;" \
					        ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CALC:MATH%d:DATA?",
	[SCPI_CMD_GET_SYS_BEEP_ON_TRIGGER]    = ":SYST:BEEP:TRIG:STAT?",
	[SCPI_CMD_SET_SYS_BEEP_ON_TRIGGER]    = ":SYST:BEEP:TRIG:STAT %d",
	[SCPI_CMD_GET_SYS_BEEP_ON_ERROR]      = ":SYST:BEEP:ERR:STAT?",
	[SCPI_CMD_SET_SYS_BEEP_ON_ERROR]      = ":SYST:BEEP:ERR:STAT %d",
};

/*
 * This dialect is used by the Rohde&Schwarz RTB2000, RTM3000 and
 * RTA4000 series.
 *
 * It doesn't support directly setting the sample rate, although
 * it supports setting the maximum sample rate (through the
 * Automatic Record Length functionality).
 *
 * It supports setting a logic threshold for Logic (Pattern)
 * Trigger on digitized analog channels (custom level).
 */
static const char *rohde_schwarz_rtb200x_rtm300x_rta400x_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		      = ":FORM UINT,8;:LOG%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		      = ":TIM:SCAL?",
	[SCPI_CMD_SET_TIMEBASE]		      = ":TIM:SCAL %s",
	[SCPI_CMD_GET_HORIZONTAL_DIV]	      = ":TIM:DIV?",
	[SCPI_CMD_GET_COUPLING]		      = ":CHAN%d:COUP?",
	[SCPI_CMD_SET_COUPLING]		      = ":CHAN%d:COUP %s",
	[SCPI_CMD_GET_SAMPLE_RATE]	      = ":ACQ:SRAT?",
	[SCPI_CMD_GET_AUTO_RECORD_LENGTH]     = ":ACQ:POIN:AUT?",
	[SCPI_CMD_SET_AUTO_RECORD_LENGTH]     = ":ACQ:POIN:AUT %d",
	[SCPI_CMD_GET_ARITHMETICS_TYPE]	      = ":CHAN:ARIT?",   /* No index needed. Don't use ACQ:TYPE ! */
	[SCPI_CMD_SET_ARITHMETICS_TYPE]	      = ":CHAN:ARIT %s", /* No index needed. Don't use ACQ:TYPE ! */
	[SCPI_CMD_GET_INTERPOLATION_MODE]     = ":ACQ:INT?",
	[SCPI_CMD_SET_INTERPOLATION_MODE]     = ":ACQ:INT %s",
	[SCPI_CMD_GET_ANALOG_DATA]	      = ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL?",
	[SCPI_CMD_SET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL %s",
	[SCPI_CMD_GET_DIG_POD_STATE]	      = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_POD_STATE]	      = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR?",
	[SCPI_CMD_SET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR %s",
	[SCPI_CMD_GET_TRIGGER_SLOPE]	      = ":TRIG:A:EDGE:SLOP?",
	[SCPI_CMD_SET_TRIGGER_SLOPE]	      = ":TRIG:A:TYPE EDGE;:TRIG:A:EDGE:SLOP %s",
	[SCPI_CMD_GET_TRIGGER_PATTERN]	      = ":TRIG:A:PATT:SOUR?",
	[SCPI_CMD_SET_TRIGGER_PATTERN]	      = ":TRIG:A:TYPE LOGIC;" \
					        ":TRIG:A:PATT:FUNC AND;" \
					        ":TRIG:A:PATT:COND \"TRUE\";" \
					        ":TRIG:A:PATT:MODE OFF;" \
					        ":TRIG:A:PATT:SOUR \"%s\"",
	[SCPI_CMD_GET_HIGH_RESOLUTION]	      = ":ACQ:HRES?",
	[SCPI_CMD_SET_HIGH_RESOLUTION]	      = ":ACQ:HRES %s",
	[SCPI_CMD_GET_PEAK_DETECTION]	      = ":ACQ:PEAK?",
	[SCPI_CMD_SET_PEAK_DETECTION]	      = ":ACQ:PEAK %s",
	[SCPI_CMD_GET_DIG_CHAN_STATE]	      = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_CHAN_STATE]	      = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_VERTICAL_OFFSET]	      = ":CHAN%d:POS?",	/* Might not be supported on RTB200x... */
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	      = ":TIM:POS?",
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	      = ":TIM:POS %s",
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT?",
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT %d",
	[SCPI_CMD_GET_PROBE_UNIT]	      = ":PROB%d:SET:ATT:UNIT?",
	[SCPI_CMD_GET_ANALOG_THRESHOLD]	      = ":CHAN%d:THR?",
	[SCPI_CMD_SET_ANALOG_THRESHOLD]	      = ":CHAN%d:THR %s",
	[SCPI_CMD_GET_DIG_POD_THRESHOLD]      = ":DIG%d:TECH?",
	[SCPI_CMD_SET_DIG_POD_THRESHOLD]      = ":DIG%d:TECH %s",
	[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD] = ":DIG%d:THR?",
	[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD] = ":DIG%d:THR %s",
	[SCPI_CMD_GET_BANDWIDTH_LIMIT]	      = ":CHAN%d:BAND?",
	[SCPI_CMD_SET_BANDWIDTH_LIMIT]	      = ":CHAN%d:BAND %s",
	[SCPI_CMD_GET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR?",
	[SCPI_CMD_SET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR:DEF \"%s\"",
	[SCPI_CMD_GET_FFT_SAMPLE_RATE]	      = ":CALC:MATH%d:FFT:SRAT?",
	[SCPI_CMD_SET_FFT_SAMPLE_RATE]	      = ":CALC:MATH%d:FFT:SRAT %s",
	[SCPI_CMD_GET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE?",
	[SCPI_CMD_SET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP?",
	[SCPI_CMD_SET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN?",
	[SCPI_CMD_SET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR %s",
	[SCPI_CMD_GET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:ADJ?",
	[SCPI_CMD_SET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:VAL %s",
	[SCPI_CMD_GET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO %d",
	[SCPI_CMD_GET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT %d",
	[SCPI_CMD_GET_FFT_DATA]		      = ":CALC:MATH%d:ARIT OFF;" \
					        ":CALC:MATH%d:FFT:MAGN:SCAL DBM;" \
					        ":CALC:MATH%d:SCAL 20;" \
					        ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CALC:MATH%d:DATA?",
	[SCPI_CMD_GET_SYS_BEEP_ON_TRIGGER]    = ":SYST:BEEP:TRIG:STAT?",
	[SCPI_CMD_SET_SYS_BEEP_ON_TRIGGER]    = ":SYST:BEEP:TRIG:STAT %d",
	[SCPI_CMD_GET_SYS_BEEP_ON_ERROR]      = ":SYST:BEEP:ERR:STAT?",
	[SCPI_CMD_SET_SYS_BEEP_ON_ERROR]      = ":SYST:BEEP:ERR:STAT %d",
};

/*
 * This dialect is used by the Rohde&Schwarz RTO2000 series.
 *
 * It supports setting the sample rate directly to any desired
 * value up to the maximum allowed.
 *
 * It doesn't provide a separate setting for the FFT sample rate
 * as in the HMO, RTC1000, RTB2000, RTM3000 and RTA4000 series.
 *
 * The Logic (Pattern) Trigger doesn't use the analog channels
 * as possible sources, therefore the threshold can be set only
 * for digital channels (bus).
 *
 * At the moment the High Resolution and Peak Detection modes
 * are not implemented.
 */
static const char *rohde_schwarz_rto200x_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		      = ":LOG%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		      = ":TIM:SCAL?",
	[SCPI_CMD_SET_TIMEBASE]		      = ":TIM:SCAL %s",
	[SCPI_CMD_GET_HORIZONTAL_DIV]	      = ":TIM:DIV?",
	[SCPI_CMD_GET_COUPLING]		      = ":CHAN%d:COUP?",
	[SCPI_CMD_SET_COUPLING]		      = ":CHAN%d:COUP %s",
	[SCPI_CMD_GET_SAMPLE_RATE]	      = ":ACQ:SRAT?",
	[SCPI_CMD_SET_SAMPLE_RATE]	      = ":ACQ:SRAT %s",
	[SCPI_CMD_GET_INTERPOLATION_MODE]     = ":ACQ:INT?",
	[SCPI_CMD_SET_INTERPOLATION_MODE]     = ":ACQ:INT %s",
	[SCPI_CMD_GET_ANALOG_DATA]	      = ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL?",
	[SCPI_CMD_SET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL %s",
	[SCPI_CMD_GET_DIG_POD_STATE]	      = ":BUS%d:PAR:STAT?",
	[SCPI_CMD_SET_DIG_POD_STATE]	      = ":BUS%d:PAR:BIT%d:STAT %d",
	[SCPI_CMD_GET_TRIGGER_SOURCE]	      = ":TRIG1:SOUR?",
	[SCPI_CMD_SET_TRIGGER_SOURCE]	      = ":TRIG1:SOUR %s",
	[SCPI_CMD_GET_TRIGGER_SLOPE]	      = ":TRIG1:EDGE:SLOP?",
	[SCPI_CMD_SET_TRIGGER_SLOPE]	      = ":TRIG1:TYPE EDGE;:TRIG1:EDGE:SLOP %s",
	[SCPI_CMD_GET_TRIGGER_PATTERN]	      = ":TRIG1:PAR:PATT:BIT%d?",
	[SCPI_CMD_SET_TRIGGER_PATTERN]	      = ":TRIG1:PAR:TYPE PATT;" \
					        ":TRIG1:PAR:PATT:MODE OFF;" \
					        ":TRIG1:PAR:PATT:BIT%d %s",
/* TODO: High Resolution and Peak Detection modes are based on channel and waveform number. */
	[SCPI_CMD_GET_DIG_CHAN_STATE]	      = ":BUS%d:PAR:BIT%d:STAT?",
	[SCPI_CMD_SET_DIG_CHAN_STATE]	      = ":BUS%d:PAR:BIT%d:STAT %d",
	[SCPI_CMD_GET_VERTICAL_OFFSET]	      = ":CHAN%d:POS?",
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	      = ":TIM:HOR:POS?",
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	      = ":TIM:HOR:POS %s",
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT?",
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT %d",
	[SCPI_CMD_GET_PROBE_UNIT]	      = ":PROB%d:SET:ATT:UNIT?",
	[SCPI_CMD_GET_DIG_POD_THRESHOLD]      = ":BUS%d:PAR:TECH?",
	[SCPI_CMD_SET_DIG_POD_THRESHOLD]      = ":BUS%d:PAR:TECH %s",
	[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD] = ":BUS%d:PAR:THR%d?",
	[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD] = ":BUS%d:PAR:THR%d %s",
	[SCPI_CMD_GET_BANDWIDTH_LIMIT]	      = ":CHAN%d:BAND?",
	[SCPI_CMD_SET_BANDWIDTH_LIMIT]	      = ":CHAN%d:BAND %s",
	[SCPI_CMD_GET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR?",
	[SCPI_CMD_SET_MATH_EXPRESSION]	      = ":CALC:MATH%d:EXPR:DEF \"%s\"",
/*	[SCPI_CMD_GET_FFT_SAMPLE_RATE] missing, as of User Manual version 12 ! */
/*	[SCPI_CMD_SET_FFT_SAMPLE_RATE] missing, as of User Manual version 12 ! */
	[SCPI_CMD_GET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE?",
	[SCPI_CMD_SET_FFT_WINDOW_TYPE]	      = ":CALC:MATH%d:FFT:WIND:TYPE %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_START]    = ":CALC:MATH%d:FFT:STAR %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP?",
	[SCPI_CMD_SET_FFT_FREQUENCY_STOP]     = ":CALC:MATH%d:FFT:STOP %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN?",
	[SCPI_CMD_SET_FFT_FREQUENCY_SPAN]     = ":CALC:MATH%d:FFT:SPAN %s",
	[SCPI_CMD_GET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR?",
	[SCPI_CMD_SET_FFT_FREQUENCY_CENTER]   = ":CALC:MATH%d:FFT:CFR %s",
	[SCPI_CMD_GET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:ADJ?",
	[SCPI_CMD_SET_FFT_RESOLUTION_BW]      = ":CALC:MATH%d:FFT:BAND:RES:VAL %s",
	[SCPI_CMD_GET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_COUPLING]  = ":CALC:MATH%d:FFT:BAND:RES:AUTO %d",
	[SCPI_CMD_GET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT?",
	[SCPI_CMD_SET_FFT_SPAN_RBW_RATIO]     = ":CALC:MATH%d:FFT:BAND:RES:RAT %d",
	[SCPI_CMD_GET_FFT_DATA]		      = ":CALC:MATH%d:ARIT OFF;" \
					        ":CALC:MATH%d:FFT:MAGN:SCAL DBM;" \
					        ":CALC:MATH%d:VERT:SCAL 20;" \
					        ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CALC:MATH%d:DATA?",
};

/* Options currently supported on the HMO2524 and HMO3000 series. */
static const uint32_t devopts_hmo300x[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_WAVEFORM_SAMPLE_RATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_RANDOM_SAMPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ACQUISITION_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ARITHMETICS_TYPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_INTERPOLATION_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_PATTERN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_HIGH_RESOLUTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PEAK_DETECTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_WINDOW | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_FFT_FREQUENCY_START | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_STOP | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_SPAN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_CENTER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_RESOLUTION_BW | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_COUPLING | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_BEEP_ON_TRIGGER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_BEEP_ON_ERROR | SR_CONF_GET | SR_CONF_SET,
};

/* Options currently supported on the HMO Compact, HMO1x02 and RTC1000 series. */
static const uint32_t devopts_hmocompact_hmo1x02_rtc100x[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_WAVEFORM_SAMPLE_RATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ACQUISITION_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ARITHMETICS_TYPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_INTERPOLATION_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_PATTERN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_HIGH_RESOLUTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PEAK_DETECTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_WINDOW | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_FFT_FREQUENCY_START | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_STOP | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_SPAN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_CENTER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_RESOLUTION_BW | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_COUPLING | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_BEEP_ON_TRIGGER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_BEEP_ON_ERROR | SR_CONF_GET | SR_CONF_SET,
};

/* Options currently supported on the RTB200x, RTM300x and RTA400x series. */
static const uint32_t devopts_rtb200x_rtm300x_rta400x[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_AUTO_RECORD_LENGTH | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_ARITHMETICS_TYPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_INTERPOLATION_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_PATTERN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_HIGH_RESOLUTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PEAK_DETECTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_WINDOW | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_FFT_FREQUENCY_START | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_STOP | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_SPAN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_CENTER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_RESOLUTION_BW | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_COUPLING | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_BEEP_ON_TRIGGER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_BEEP_ON_ERROR | SR_CONF_GET | SR_CONF_SET,
};

/* Options currently supported on the RTO200x series. */
static const uint32_t devopts_rto200x[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_INTERPOLATION_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_PATTERN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
/*	SR_CONF_HIGH_RESOLUTION | SR_CONF_GET | SR_CONF_SET, */ /* Not implemented yet. */
/*	SR_CONF_PEAK_DETECTION | SR_CONF_GET | SR_CONF_SET, */ /* Not implemented yet. */
	SR_CONF_FFT_WINDOW | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_FFT_FREQUENCY_START | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_STOP | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_SPAN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_FREQUENCY_CENTER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_RESOLUTION_BW | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_COUPLING | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FFT_SPAN_RBW_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VSCALE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ANALOG_THRESHOLD_CUSTOM | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_BANDWIDTH_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_analog_rto200x[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VSCALE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_BANDWIDTH_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_digital[] = {
	SR_CONF_LOGIC_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LOGIC_THRESHOLD_CUSTOM | SR_CONF_GET | SR_CONF_SET,
};

/*
 * Waveform acquisition rate / sample rate option arrays for
 * different oscilloscope models.
 *
 * IMPORTANT: Always place the Maximum Sample Rate option
 *            (usually named "MSAM") at index position
 *            MAXIMUM_SAMPLE_RATE_INDEX (see protocol.h) !
 */

/* Segmented memory option available (manual setting). */
static const char *waveform_sample_rate[] = {
	"AUTO",
	"MWAV",
	"MSAM",
	"MAN",
};

/* RTC1000, HMO1002/1202 and HMO Compact series have no
 * segmented memory option available (no manual setting).
 */
static const char *waveform_sample_rate_nosegmem[] = {
	"AUTO",
	"MWAV",
	"MSAM",
};

/* Only available on the HMO2524 and HMO3000 series. */
static const char *random_sampling[] = {
	"AUTO",
	"OFF",
};

/* Only available on the HMO and RTC100x series. */
static const char *acquisition_mode[] = {
	"RTIM",
	"ETIM",
};

/* HMO Compact series. */
static const char *arithmetics_type_hmo_compact[] = {
	"OFF",
	"ENV",
	"AVER",
	"FILT",
};

/* HMO1002/1202, HMO2524, HMO3000 and RTC100x series. */
static const char *arithmetics_type_hmo_rtc100x[] = {
	"OFF",
	"ENV",
	"AVER",
	"SMO",
	"FILT",
};

/* RTB200x, RTM300x and RTA400x series. */
static const char *arithmetics_type_rtb200x_rtm300x_rta400x[] = {
	"OFF",
	"ENV",
	"AVER",
};

static const char *interpolation_mode[] = {
	"LIN",
	"SINX",
	"SMHD",
};

static const char *coupling_options[] = {
	"AC",  // AC with 50 Ohm termination (152x, 202x, 30xx, 1202)
	"ACL", // AC with 1 MOhm termination
	"DC",  // DC with 50 Ohm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
};

static const char *coupling_options_rtb200x[] = {
	"ACL", // AC with 1 MOhm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
};

static const char *coupling_options_rtm300x[] = {
	"ACL", // AC with 1 MOhm termination
	"DC",  // DC with 50 Ohm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
};

static const char *coupling_options_rto200x[] = {
	"AC",  // AC with 1 MOhm termination
	"DC",  // DC with 50 Ohm termination
	"DCL", // DC with 1 MOhm termination
	"GND", // Mentioned in datasheet version 03.00, but not in User Manual version 12 !
};

/*
 * The trigger slope option keywords MUST be placed in
 * the following order: Rising (first entry), Falling
 * and finally Either (last entry).
 */
static const char *scope_trigger_slopes[] = {
	"POS",
	"NEG",
	"EITH",
};

/*
 * Predefined logic thresholds for the HMO and RTC100x
 * series.
 */
static const char *logic_threshold_hmo_rtc100x[] = {
	"TTL",
	"ECL",
	"CMOS",
	"USER1",
	"USER2", // overwritten by logic_threshold_custom, use USER1 for permanent setting
};

/*
 * Predefined logic thresholds for the RTB200x, RTM300x
 * and RTA400x series.
 */
static const char *logic_threshold_rtb200x_rtm300x_rta400x[] = {
	"TTL",
	"ECL",
	"CMOS",
	"MAN", // overwritten by logic_threshold_custom
};

/*
 * Predefined logic thresholds for the RTO200x series.
 */
static const char *logic_threshold_rto200x[] = {
	"V15",  // TTL
	"V25",  // CMOS 5V
	"V165", // CMOS 3.3V
	"V125", // CMOS 2.5V
	"V09",  // CMOS 1.85V
	"VM13", // ECL -1.3V
	"V38",  // PECL
	"V20",  // LVPECL
	"V0",   // Ground
	"MAN",  // overwritten by logic_threshold_custom
};

/* FFT window types available on the HMO series */
static const char *fft_window_types_hmo[] = {
	"RECT",
	"HAMM",
	"HANN",
	"BLAC",
};

/* FFT window types available on the RT series, except RTO200x */
static const char *fft_window_types_rt[] = {
	"RECT",
	"HAMM",
	"HANN",
	"BLAC",
	"FLAT",
};

/* FFT window types available on the RTO200x */
static const char *fft_window_types_rto200x[] = {
	"RECT",
	"HAMM",
	"HANN",
	"BLAC",
	"GAUS",
	"FLAT",
	"KAIS",
};

/* Bandwidth limits for all series except the RTO200x */
static const char *bandwidth_limit[] = {
	"FULL",
	"B20",
};

/* Bandwidth limits for the RTO200x */
static const char *bandwidth_limit_rto200x[] = {
	"FULL",
	"B20",
	"B200",
	"B800", // available only for 50 Ohm coupling when bandwidth >= 1GHz
};

/* HMO1002/HMO1202 */
static const char *an2_dig8_trigger_sources_hmo1x02[] = {
	"CH1", "CH2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"LINE", "EXT", "PATT", "NONE",
};

/* HMO Compact2 */
static const char *an2_dig8_trigger_sources_hmo_compact2[] = {
	"CH1", "CH2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"LINE", "EXT", "PATT", "BUS1", "BUS2", "NONE",
};

/* RTC1002 */
static const char *an2_dig8_trigger_sources_rtc100x[] = {
	"CH1", "CH2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"LINE", "EXT", "PATT", "NONE",
};

/* HMO3xx2 */
static const char *an2_dig16_trigger_sources[] = {
	"CH1", "CH2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
	"LINE", "EXT", "PATT", "BUS1", "BUS2", "NONE",
};

/* RTB2002 and RTM3002 */
static const char *an2_dig16_sbus_trigger_sources[] = {
	"CH1", "CH2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
	"LINE", "EXT", "SBUS1", "SBUS2",
};

/* HMO Compact4 */
static const char *an4_dig8_trigger_sources_hmo_compact4[] = {
	"CH1", "CH2", "CH3", "CH4",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"LINE", "EXT", "PATT", "BUS1", "BUS2", "NONE",
};

/* HMO3xx4 and HMO2524 */
static const char *an4_dig16_trigger_sources[] = {
	"CH1", "CH2", "CH3", "CH4",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
	"LINE", "EXT", "PATT", "BUS1", "BUS2", "NONE",
};

/* RTB2004, RTM3004 and RTA4004 */
static const char *an4_dig16_sbus_trigger_sources[] = {
	"CH1", "CH2", "CH3", "CH4",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
	"LINE", "EXT", "SBUS1", "SBUS2",
};

/* RTO200x */
static const char *rto200x_trigger_sources[] = {
	"CHAN1", "CHAN2", "CHAN3", "CHAN4",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
	"MSOB1", "MSOB2", "MSOB3", "MSOB4",
	"EXT", "LOGIC", "SBUS",
};

static const uint64_t timebases[][2] = {
	/* nanoseconds */
	{ 1, 1000000000 },
	{ 2, 1000000000 },
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 200, 1000000000 },
	{ 500, 1000000000 },
	/* microseconds */
	{ 1, 1000000 },
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
};

/* HMO Compact series (HMO722/724/1022/1024/1522/1524/2022/2024) do
 * not support 1 ns timebase setting.
 */
static const uint64_t timebases_hmo_compact[][2] = {
	/* nanoseconds */
	{ 2, 1000000000 },
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 200, 1000000000 },
	{ 500, 1000000000 },
	/* microseconds */
	{ 1, 1000000 },
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
};

/* RTO200x: from 25E-12 to 10000 s/div with 1E-12 increments */
static const uint64_t timebases_rto200x[][2] = {
	/* picoseconds */
	{ 25, 1000000000000 },
	{ 50, 1000000000000 },
	{ 100, 1000000000000 },
	{ 200, 1000000000000 },
	{ 500, 1000000000000 },
	/* nanoseconds */
	{ 1, 1000000000 },
	{ 2, 1000000000 },
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 200, 1000000000 },
	{ 500, 1000000000 },
	/* microseconds */
	{ 1, 1000000 },
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
	{ 100, 1 },
	{ 200, 1 },
	{ 500, 1 },
	{ 1000, 1 },
	{ 2000, 1 },
	{ 5000, 1 },
	{ 10000, 1 },
};

static const uint64_t vscale[][2] = {
	/* millivolts / div */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* volts / div */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
};

static const char *scope_analog_channel_names[] = {
	"CH1", "CH2", "CH3", "CH4",
};

static const char *scope_digital_channel_names[] = {
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

static struct scope_config scope_models[] = {
	{
		/* HMO Compact2: HMO722/1022/1522/2022 support only 8 digital channels. */
		.name = {"HMO722", "HMO1022", "HMO1522", "HMO2022", NULL},
		.analog_channels = 2,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_hmocompact_hmo1x02_rtc100x,
		.num_devopts = ARRAY_SIZE(devopts_hmocompact_hmo1x02_rtc100x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate_nosegmem,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate_nosegmem),

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		.acquisition_mode = &acquisition_mode,
		.num_acquisition_mode = ARRAY_SIZE(acquisition_mode),

		.arithmetics_type = &arithmetics_type_hmo_compact,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_hmo_compact),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold_hmo_rtc100x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_hmo_rtc100x),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig8_trigger_sources_hmo_compact2,
		.num_trigger_sources = ARRAY_SIZE(an2_dig8_trigger_sources_hmo_compact2),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_hmo,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_hmo),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases_hmo_compact,
		.num_timebases = ARRAY_SIZE(timebases_hmo_compact),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_scpi_dialect,
	},
	{
		/* HMO1002/HMO1202 support only 8 digital channels. */
		.name = {"HMO1002", "HMO1202", NULL},
		.analog_channels = 2,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_hmocompact_hmo1x02_rtc100x,
		.num_devopts = ARRAY_SIZE(devopts_hmocompact_hmo1x02_rtc100x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate_nosegmem,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate_nosegmem),

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		.acquisition_mode = &acquisition_mode,
		.num_acquisition_mode = ARRAY_SIZE(acquisition_mode),

		.arithmetics_type = &arithmetics_type_hmo_rtc100x,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_hmo_rtc100x),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold_hmo_rtc100x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_hmo_rtc100x),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig8_trigger_sources_hmo1x02,
		.num_trigger_sources = ARRAY_SIZE(an2_dig8_trigger_sources_hmo1x02),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_hmo,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_hmo),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_scpi_dialect,
	},
	{
		/* RTC1002 supports only 8 digital channels. */
		.name = {"RTC1002", NULL},
		.analog_channels = 2,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_hmocompact_hmo1x02_rtc100x,
		.num_devopts = ARRAY_SIZE(devopts_hmocompact_hmo1x02_rtc100x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate_nosegmem,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate_nosegmem),

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		.acquisition_mode = &acquisition_mode,
		.num_acquisition_mode = ARRAY_SIZE(acquisition_mode),

		.arithmetics_type = &arithmetics_type_hmo_rtc100x,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_hmo_rtc100x),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold_hmo_rtc100x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_hmo_rtc100x),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig8_trigger_sources_rtc100x,
		.num_trigger_sources = ARRAY_SIZE(an2_dig8_trigger_sources_rtc100x),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_scpi_dialect,
	},
	{
		/* HMO3032/3042/3052/3522 support 16 digital channels. */
		.name = {"HMO3032", "HMO3042", "HMO3052", "HMO3522", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_hmo300x,
		.num_devopts = ARRAY_SIZE(devopts_hmo300x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate),

		.random_sampling = &random_sampling,
		.num_random_sampling = ARRAY_SIZE(random_sampling),

		.acquisition_mode = &acquisition_mode,
		.num_acquisition_mode = ARRAY_SIZE(acquisition_mode),

		.arithmetics_type = &arithmetics_type_hmo_rtc100x,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_hmo_rtc100x),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold_hmo_rtc100x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_hmo_rtc100x),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig16_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig16_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		/* FlatTop window available, but not listed in User Manual version 04. */
		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_scpi_dialect,
	},
	{
		/* HMO Compact4: HMO724/1024/1524/2024 support only 8 digital channels. */
		.name = {"HMO724", "HMO1024", "HMO1524", "HMO2024", NULL},
		.analog_channels = 4,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_hmocompact_hmo1x02_rtc100x,
		.num_devopts = ARRAY_SIZE(devopts_hmocompact_hmo1x02_rtc100x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate_nosegmem,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate_nosegmem),

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		.acquisition_mode = &acquisition_mode,
		.num_acquisition_mode = ARRAY_SIZE(acquisition_mode),

		.arithmetics_type = &arithmetics_type_hmo_compact,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_hmo_compact),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold_hmo_rtc100x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_hmo_rtc100x),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an4_dig8_trigger_sources_hmo_compact4,
		.num_trigger_sources = ARRAY_SIZE(an4_dig8_trigger_sources_hmo_compact4),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_hmo,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_hmo),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases_hmo_compact,
		.num_timebases = ARRAY_SIZE(timebases_hmo_compact),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_scpi_dialect,
	},
	{
		.name = {"HMO2524", "HMO3034", "HMO3044", "HMO3054", "HMO3524", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_hmo300x,
		.num_devopts = ARRAY_SIZE(devopts_hmo300x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.waveform_sample_rate = &waveform_sample_rate,
		.num_waveform_sample_rate = ARRAY_SIZE(waveform_sample_rate),

		.random_sampling = &random_sampling,
		.num_random_sampling = ARRAY_SIZE(random_sampling),

		.acquisition_mode = &acquisition_mode,
		.num_acquisition_mode = ARRAY_SIZE(acquisition_mode),

		.arithmetics_type = &arithmetics_type_hmo_rtc100x,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_hmo_rtc100x),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold_hmo_rtc100x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_hmo_rtc100x),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an4_dig16_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_hmo,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_hmo),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_scpi_dialect,
	},
	{
		.name = {"RTB2002", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_rtb200x_rtm300x_rta400x,
		.num_devopts = ARRAY_SIZE(devopts_rtb200x_rtm300x_rta400x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		/* Acquisition mode not available. */
		.num_acquisition_mode = 0,

		.arithmetics_type = &arithmetics_type_rtb200x_rtm300x_rta400x,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_rtb200x_rtm300x_rta400x),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtb200x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtb200x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x_rta400x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x_rta400x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an2_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		/* FFT support status unclear as of User Manual version 06. */
		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_rtb200x_rtm300x_rta400x_scpi_dialect,
	},
	{
		.name = {"RTB2004", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_rtb200x_rtm300x_rta400x,
		.num_devopts = ARRAY_SIZE(devopts_rtb200x_rtm300x_rta400x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		/* Acquisition mode not available. */
		.num_acquisition_mode = 0,

		.arithmetics_type = &arithmetics_type_rtb200x_rtm300x_rta400x,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_rtb200x_rtm300x_rta400x),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtb200x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtb200x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x_rta400x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x_rta400x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an4_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		/* FFT support status unclear as of User Manual version 06. */
		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_rtb200x_rtm300x_rta400x_scpi_dialect,
	},
	{
		.name = {"RTM3002", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_rtb200x_rtm300x_rta400x,
		.num_devopts = ARRAY_SIZE(devopts_rtb200x_rtm300x_rta400x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		/* Acquisition mode not available. */
		.num_acquisition_mode = 0,

		.arithmetics_type = &arithmetics_type_rtb200x_rtm300x_rta400x,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_rtb200x_rtm300x_rta400x),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtm300x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtm300x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x_rta400x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x_rta400x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an2_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_rtb200x_rtm300x_rta400x_scpi_dialect,
	},
	{
		.name = {"RTM3004", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_rtb200x_rtm300x_rta400x,
		.num_devopts = ARRAY_SIZE(devopts_rtb200x_rtm300x_rta400x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		/* Acquisition mode not available. */
		.num_acquisition_mode = 0,

		.arithmetics_type = &arithmetics_type_rtb200x_rtm300x_rta400x,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_rtb200x_rtm300x_rta400x),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtm300x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtm300x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x_rta400x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x_rta400x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an4_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_rtb200x_rtm300x_rta400x_scpi_dialect,
	},
	{
		.name = {"RTA4004", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_rtb200x_rtm300x_rta400x,
		.num_devopts = ARRAY_SIZE(devopts_rtb200x_rtm300x_rta400x),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		/* Acquisition mode not available. */
		.num_acquisition_mode = 0,

		.arithmetics_type = &arithmetics_type_rtb200x_rtm300x_rta400x,
		.num_arithmetics_type = ARRAY_SIZE(arithmetics_type_rtb200x_rtm300x_rta400x),

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rtm300x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtm300x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x_rta400x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x_rta400x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an4_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		/* FFT support status unclear as of User Manual version 03. */
		.fft_window_types = &fft_window_types_rt,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rt),

		.bandwidth_limit = &bandwidth_limit,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_rtb200x_rtm300x_rta400x_scpi_dialect,
	},
	{
		/* For RTO200x, number of analog channels is specified in the serial number, not in the name. */
		.name = {"RTO", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts_rto200x,
		.num_devopts = ARRAY_SIZE(devopts_rto200x),

		.devopts_cg_analog = &devopts_cg_analog_rto200x,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog_rto200x),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		/* Waveform acquisition rate / sample rate setting not available. */
		.num_waveform_sample_rate = 0,

		/* Random Sampling not available. */
		.num_random_sampling = 0,

		/* Acquisition mode not available. */
		.num_acquisition_mode = 0,

		/* Arithmetics type not available. */
		.num_arithmetics_type = 0,

		.interpolation_mode = &interpolation_mode,
		.num_interpolation_mode = ARRAY_SIZE(interpolation_mode),

		.coupling_options = &coupling_options_rto200x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rto200x),

		.logic_threshold = &logic_threshold_rto200x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rto200x),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &rto200x_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(rto200x_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.fft_window_types = &fft_window_types_rto200x,
		.num_fft_window_types = ARRAY_SIZE(fft_window_types_rto200x),

		.bandwidth_limit = &bandwidth_limit_rto200x,
		.num_bandwidth_limit = ARRAY_SIZE(bandwidth_limit_rto200x),

		.timebases = &timebases_rto200x,
		.num_timebases = ARRAY_SIZE(timebases_rto200x),

		.vscale = &vscale,
		.num_vscale = ARRAY_SIZE(vscale),

		.num_ydivs = 10,

		.scpi_dialect = &rohde_schwarz_rto200x_scpi_dialect,
	},
};

