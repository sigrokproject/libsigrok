/* SPDX-License-Identifier: GPL-3.0-only */
/* Hardware-independent checks; golden CRCs are captured DLA-32 traffic,
 * expected samples are generated independently in the time domain. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <config.h>
#include <check.h>
#include "lib.h"
#include "../src/hardware/fnirsi-dla32/protocol.h"

#define CHECK(expr) ck_assert(expr)

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void golden_commands(void)
{
	uint8_t packet[2048], trigger[32] = {0};
	const uint8_t on[] = {0x0a,0x17,0x0b,0x00,0x10,0x40,0x4b,0x4c,0x00,0x14,0x00,0x00,0x00,0x0b,0x08,0xd7,0xee,0xc0};
	const uint8_t off[] = {0x0a,0x17,0x03,0x00,0x10,0x0b,0xe4,0x3e,0xc0,0x2d};
	const uint32_t samples[] = {50000,100000,250000,500000};
	const uint32_t crc[] = {0xbd356d1a,0x53da43d7,0xfd110ef8,0x73775bce};
	unsigned int i;
	struct dla_setup_config cfg = {0};

	for (i = 0; i < 4; i++) {
		dla_setup(packet,160,2,samples[i]);
		CHECK(get32(packet+41) == crc[i]);
		CHECK(get32(packet+32) == samples[i]);
	}
	dla_setup(packet,60,7,50000); CHECK(get32(packet+41)==0x5b5d739f);
	dla_setup(packet,250,1,50000); CHECK(get32(packet+41)==0x1256301f);
	cfg.samplerate=200000000; cfg.samplerate_index=11; cfg.threshold=160;
	cfg.threshold_range=2; cfg.interval_ms=3; cfg.duration_ms=1;
	cfg.samples=200000; cfg.buffer=1; cfg.trigger_position=10;
	dla_setup_configure(packet,&cfg); CHECK(get32(packet+41)==0x50401304);
	cfg.samplerate=1000000000; cfg.samplerate_index=14; cfg.samples=1000000;
	dla_setup_configure(packet,&cfg); CHECK(get32(packet+41)==0x5f257f20);
	cfg.samples=UINT64_C(10000000000); dla_setup_configure(packet,&cfg);
	CHECK(get32(packet+32)==UINT32_C(0x540be400)); CHECK(get32(packet+36)==2);
	dla_stop(packet); CHECK(get32(packet+13)==0xd724fce6);
	CHECK(dla_pwm(packet,0,5000000,20,1)==0); CHECK(!memcmp(packet+8,on,sizeof(on)));
	CHECK(dla_pwm(packet,0,0,0,0)==0); CHECK(!memcmp(packet+8,off,sizeof(off)));
	for (i=0;i<4;i++) { CHECK(dla_pwm(packet,i,1000,50,1)==0); CHECK(packet[12]==0x10+i); }
	CHECK(dla_pwm(packet,4,1000,50,1)<0);
	CHECK(dla_pwm(packet,0,0,50,1)<0); CHECK(dla_pwm(packet,0,20000001,50,1)<0);
	CHECK(dla_pwm(packet,0,1000,0,1)<0); CHECK(dla_pwm(packet,0,1000,100,1)<0);
	trigger[31]=3; CHECK(dla_channels_config(packet,UINT32_C(0x80000001),trigger,0)==0);
	CHECK(get32(packet+13)==UINT32_C(0x80000001)); CHECK(packet[48]==3);
	CHECK(dla_channels_config(packet,0,trigger,0)<0);
	trigger[31]=6; CHECK(dla_channels_config(packet,UINT32_MAX,trigger,0)<0);
}

static void decode_case(uint32_t mask, unsigned int stream)
{
	struct dla_decoder decoder;
	uint8_t wire[64], decoded[64], actual[64], tail[32], pattern;
	uint32_t expected[16]={0}, value;
	unsigned int ch, lane=0, frame, t, b;
	size_t split, pending, first, second;
	CHECK(dla_decoder_init(&decoder,mask,stream)==0);
	memset(wire,0xff,sizeof(wire));
	for (ch=0;ch<32;ch++) {
		if (!(mask & (UINT32_C(1)<<ch))) continue;
		for (frame=0;frame<2;frame++) {
			pattern=(uint8_t)(0xa6U^(ch*29U+frame*83U));
			wire[frame*decoder.wire_channels+lane]=pattern;
			for (t=0;t<8;t++) if (pattern & (1U<<t)) expected[frame*8+t]|=UINT32_C(1)<<ch;
		}
		lane++;
	}
	memset(decoded,0xcc,sizeof(decoded));
	dla_decode_frames(&decoder,decoded,wire,2);
	for (t=0;t<16;t++) {
		value=0;
		for (b=0;b<decoder.unitsize;b++) value|=(uint32_t)decoded[t*decoder.unitsize+b]<<(b*8);
		CHECK(value==expected[t]);
	}
	for (split=0;split<=decoder.wire_channels*2;split++) {
		pending=first=second=0;
		CHECK(dla_decode_feed(&decoder,tail,&pending,actual,sizeof(actual),wire,split,&first)==0);
		CHECK(dla_decode_feed(&decoder,tail,&pending,actual+first,sizeof(actual)-first,wire+split,decoder.wire_channels*2-split,&second)==0);
		CHECK(pending==0); CHECK(first+second==16*decoder.unitsize);
		CHECK(!memcmp(actual,decoded,first+second));
	}
	pending=0;
	CHECK(dla_decode_feed(&decoder,tail,&pending,actual,0,wire,decoder.wire_channels,&first)<0);
	CHECK(pending==0);
}

START_TEST(test_protocol)
{
	const uint32_t masks[]={1,3,5,0x81,0x101,0x8000,0x10000,0x80000000,0x80000001,0x80010081,0xffffffff};
	struct dla_decoder decoder;
	unsigned int i, mode;
	uint32_t random=0x13579bdf;
	golden_commands();
	CHECK(dla_decoder_init(&decoder,0,0)<0);
	for (mode=0;mode<2;mode++) {
		for (i=0;i<32;i++) decode_case(UINT32_C(1)<<i,mode);
		for (i=0;i<sizeof(masks)/sizeof(masks[0]);i++) decode_case(masks[i],mode);
		for (i=0;i<256;i++) {
			random^=random<<13; random^=random>>17; random^=random<<5;
			decode_case(random,mode);
		}
	}
}
END_TEST

Suite *suite_fnirsi_dla32(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("fnirsi-dla32");
	tc = tcase_create("protocol");
	tcase_add_test(tc, test_protocol);
	suite_add_tcase(s, tc);
	return s;
}
