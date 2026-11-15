// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Air band AM receive helpers (radio_am.h; docs/spec/airband-and-broadcast.md 3)
#include "board.h"
#include "radio/radio_am.h"
#include "config.h"

bool radio_am_band(uint32_t hz)
{
	return hz >= RADIO_AM_LO_HZ && hz < RADIO_AM_HI_HZ;
}

bool radio_am_capable(void)
{
	return CONFIG_AM_RX && (board.caps & CAP_AM_RX) != 0 && board.radio != NULL && board.radio->rx_am != NULL;
}

bool radio_am_channel(const channel_t *ch)
{
	return radio_am_capable() && ch->mode == RADIO_MODE_FM && radio_am_band(ch->rx_hz);
}

bool radio_am_tx_refused(uint32_t hz)
{
	return radio_am_capable() && radio_am_band(hz);
}

// Grid point n of the 8.33 kHz grid is n x 25000 / 3 Hz, rounded to the Hz
static uint32_t grid_hz(int64_t n)
{
	return (uint32_t)((n * 25000 + 1) / 3);
}

uint32_t radio_am_tune_hz(uint32_t hz)
{
	if (!radio_am_band(hz) || hz % 5000u != 0) {
		return hz;
	}
	uint32_t ch = radio_am_name_hz(hz / 1000u);
	return ch != 0 ? ch : hz;
}

uint32_t radio_am_step_833(uint32_t hz, int dir)
{
	// From the channel a name stands for, the next grid point beyond it in dir. A codeplug keeps 10 Hz units
	// (118.00833), so within 10 Hz of a grid point is on it
	int64_t f = radio_am_tune_hz(hz);
	int64_t n = f * 3 / 25000;	// grid_hz(n) <= f
	if (dir > 0) {
		while (grid_hz(n) <= f + 10) {
			n++;
		}
		n += dir - 1;
	} else if (dir < 0) {
		while (n > 0 && grid_hz(n) >= f - 10) {
			n--;
		}
		n += dir + 1;
	}
	if (n < 0) {
		n = 0;
	}
	uint32_t next = grid_hz(n);
	if (!radio_am_band(next)) {
		return next;
	}
	// In the air band, the 8.33 kHz name: the block's 25 kHz channel is named as its first 8.33 kHz one (118.005)
	return (uint32_t)(n / 3) * 25000u + 5000u + (uint32_t)(n % 3) * 5000u;
}
