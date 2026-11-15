// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Air band AM receive (CONFIG_AM_RX; docs/spec/airband-and-broadcast.md 3). AM is not stored in the codeplug: an FM
// channel or the VFO tuned inside the aeronautical band is received in AM on a radio that can (CAP_AM_RX and
// board.radio->rx_am), and it never transmits there. The helpers are pure (the board descriptor aside) so the radio
// core and its tests share them.
#ifndef RADIO_AM_H
#define RADIO_AM_H

#include <stdint.h>
#include <stdbool.h>
#include "codeplug/codeplug.h"

// The aeronautical VHF band (ITU Radio Regulations: 108-117.975 MHz radio navigation, 117.975-137 MHz aeronautical
// mobile), AM everywhere in the world. hi is exclusive
#define RADIO_AM_LO_HZ		108000000u
#define RADIO_AM_HI_HZ		137000000u

// Air band channel steps: 25 kHz, and the 8.33 kHz spacing (25 kHz / 3) used in Europe. RADIO_AM_STEP_833 is the
// step value that means "the 8.33 kHz grid" (radio_am_step keeps the frequency on the exact grid)
#define RADIO_AM_STEP_25K	25000u
#define RADIO_AM_STEP_833	8333u

// hz is in the air band
bool radio_am_band(uint32_t hz);
// This radio receives AM (CAP_AM_RX and board.radio->rx_am)
bool radio_am_capable(void);
// The channel (or the VFO's settings) is received in AM here: an FM channel in the air band on a radio that can
bool radio_am_channel(const channel_t *ch);
// Transmitting on hz is refused because it is in the air band of a radio that receives AM there
bool radio_am_tx_refused(uint32_t hz);
// One VFO step from hz in dir with step_hz. The 8.33 kHz step moves to the next 8.33 kHz channel in dir (the exact
// 25/3 kHz grid); in the air band it lands on that channel's name (118.005, 118.010, 118.015, 118.030), elsewhere on
// the grid frequency rounded to the Hz. A name is read as its channel first, and a frequency off the grid steps to the
// nearest channel in dir. Other steps add step_hz, so 25 kHz takes 118.010 to 118.035
uint32_t radio_am_step(uint32_t hz, int dir, uint32_t step_hz);

// Air band channel names (docs/spec/airband-and-broadcast.md 3.4). With 8.33 kHz spacing a channel is known by a name
// that looks like a frequency but isn't one: each 25 kHz block holds the names ending in 0 kHz (the 25 kHz channel), 5,
// 10 and 15 kHz (the three 8.33 kHz channels, at +0, +8.333 and +16.667 kHz); a name ending in 20 kHz is no channel.
// The codeplug and the VFO keep the name as typed (118.010); only the receiver is tuned to the channel (118.008333)

// The frequency of the channel named name_khz (118010 for 118.010), 0 when the name is no channel. Any kHz value maps;
// the caller decides it is in the air band. Inline and pure so the UI's entry checks a name without the radio core
static inline uint32_t radio_am_name_hz(uint32_t name_khz)
{
	uint32_t sub = name_khz % 25u;
	if (sub % 5u != 0 || sub == 20u) {
		return 0;
	}
	uint32_t n = (name_khz - sub) / 25u * 3u + (sub <= 5u ? 0u : sub / 5u - 1u);
	return (uint32_t)(((uint64_t)n * 25000u + 1u) / 3u);
}

// The frequency the receiver tunes for hz: in the air band a whole 5 kHz value that is a channel name is that channel
// (118.010 is 118.008333, 118.005 is 118.000); anything else is tuned as stored (118.0055, 118.00833, 118.020)
uint32_t radio_am_tune_hz(uint32_t hz);

#endif
