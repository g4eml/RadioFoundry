// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The permanent "All channels" zone (CONFIG_ALL_CHANNELS_ZONE; core/codeplug/codeplug.c) on the real codeplug over a
// simulated NOR flash: always the last zone, every used channel 1..1024 in number order read from the in-use bitmap
// (forwards, backwards, after a channel is deleted), read only for the zone editor, and the saved zone index following
// it when zones are added before it or deleted.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codeplug/codeplug.h"
#include "codeplug/zone_edit.h"
#include "settings/settings.h"
#include "storage/storage.h"
#include "sim/sim_flash.h"

#if !CONFIG_ALL_CHANNELS_ZONE
#error "allChannelsZoneTest is built with CONFIG_ALL_CHANNELS_ZONE"
#endif

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

void storage_lock(void) {}
void storage_unlock(void) {}
void storage_quick_lock(void) {}
void storage_quick_unlock(void) {}

#define FLASH_SIZE	0x200000u
#define EE		0x000000u
#define FL		0x020000u

static const hal_flash_map_t map = {
	.settings_addr = 0x11000u, .settings_size = 0x8000u, .journal_addr = 0x19000u,
	.openfw_eeprom = EE, .openfw_flash = FL, .openfw_end = FLASH_SIZE,
	.calibration_addr = 0x10000u, .calibration_size = 0x200u,
};

static band_t bands[] = { { 136000000, 174000000, true }, { 400000000, 480000000, true } };

const board_t board = { .name = "test", .bands = bands, .band_count = 2, .power_count = 9, .flash = &sim_flash };

static void fresh(void)
{
	sim_flash_create(FLASH_SIZE, &map);
	CHECK(storage_init());
	settings_init();
	codeplug_init();
}

static channel_t make(const char *name, uint32_t hz)
{
	channel_t c;
	memset(&c, 0, sizeof(c));
	strncpy(c.name, name, CODEPLUG_NAME_LEN);
	c.rx_hz = c.tx_hz = hz;
	c.mode = RADIO_MODE_FM;
	c.bw = RADIO_BW_12K5;
	c.power = CODEPLUG_POWER_DEFAULT;
	c.color_code = 1;
	c.timeslot = 1;
	return c;
}

static zone_t zone_of(const char *name, const uint16_t *ch, uint16_t n)
{
	zone_t z;
	memset(&z, 0, sizeof(z));
	strncpy(z.name, name, CODEPLUG_NAME_LEN);
	for (uint16_t i = 0; i < n; i++)
		z.channels[z.count++] = ch[i];
	return z;
}

// Zone index is the "All channels" zone and lists exactly these channels, read in order and then backwards
static bool every_holds(uint16_t index, const uint16_t *ch, uint16_t n)
{
	zone_t z;
	if (!codeplug_zone_every(index) || !codeplug_zone(index, &z) || !z.every || z.count != n ||
	    strcmp(z.name, "All channels") != 0)
		return false;
	for (uint16_t i = 0; i < n; i++)
		if (zone_channel(&z, i) != ch[i])
			return false;
	for (uint16_t i = n; i-- > 0;)
		if (zone_channel(&z, i) != ch[i])
			return false;
	return zone_channel(&z, n) == 0;
}

static uint16_t saved_zone(void)
{
	settings_t st;
	settings_read(&st);
	return st.zone_index;
}

static void set_saved_zone(uint16_t zi)
{
	settings_t *st = settings_begin();
	st->zone_index = zi;
	settings_end(0);
	settings_flush();
}

// The built-in set (channels 1-6): the one zone there is
static void test_builtin(void)
{
	fresh();
	CHECK(codeplug_zone_count() == 1 && codeplug_zone_stored_count() == 0 && !codeplug_zones_stored());
	CHECK(every_holds(0, (const uint16_t[]){ 1, 2, 3, 4, 5, 6 }, 6));
	char name[CODEPLUG_NAME_LEN + 1];
	CHECK(codeplug_zone_name(0, name) && strcmp(name, "All channels") == 0);
	CHECK(!codeplug_zone_every(1) && !codeplug_zone(1, &(zone_t){ 0 }));
}

// Channels far apart: the zone steps over the empty numbers between them, in either direction and by jumps
static void test_walk(void)
{
	fresh();
	static const uint16_t nums[] = { 20, 21, 700, 1024 };
	for (unsigned i = 0; i < sizeof nums / sizeof nums[0]; i++) {
		channel_t c = make("Far", 145500000);
		CHECK(codeplug_set_channel(nums[i], &c));
	}
	CHECK(every_holds(0, (const uint16_t[]){ 1, 2, 3, 4, 5, 6, 20, 21, 700, 1024 }, 10));
	zone_t z;
	CHECK(codeplug_zone(0, &z));
	CHECK(zone_channel(&z, 9) == 1024 && zone_channel(&z, 0) == 1 && zone_channel(&z, 8) == 700);
	CHECK(zone_channel(&z, 6) == 20 && zone_channel(&z, 7) == 21 && zone_channel(&z, 2) == 3);
	CHECK(zone_list_find(&z, 700) == 8 && zone_list_find(&z, 7) == -1);
	// A channel deleted: the zone read again lists one fewer
	CHECK(codeplug_delete_channel(21));
	CHECK(every_holds(0, (const uint16_t[]){ 1, 2, 3, 4, 5, 6, 20, 700, 1024 }, 9));
	// The stale copy read past its new end gives 0, never a wrong channel past the bitmap
	CHECK(zone_channel(&z, 9) == 0);
}

// Every channel number used: 1024 positions, no zone_t list behind them
static void test_full(void)
{
	fresh();
	for (uint16_t n = 7; n <= CODEPLUG_CHANNELS_MAX; n++) {
		channel_t c = make("Ch", 446000000);
		CHECK(codeplug_set_channel(n, &c));
	}
	zone_t z;
	CHECK(codeplug_zone(0, &z) && z.every && z.count == CODEPLUG_CHANNELS_MAX);
	bool ok = true;
	for (uint16_t i = 0; i < z.count; i++)
		ok = ok && zone_channel(&z, i) == i + 1u;
	CHECK(ok);
	CHECK(zone_channel(&z, 1023) == 1024 && zone_channel(&z, 511) == 512 && zone_channel(&z, 1024) == 0);
}

// Read only: no zone edit, store, copy or delete reaches it
static void test_read_only(void)
{
	fresh();
	zone_t z;
	CHECK(codeplug_zone(0, &z) && z.every);
	CHECK(!codeplug_set_zone(0, &z));
	CHECK(zone_edit_insert(0, 0, 1) == ZONE_EDIT_NO_ZONE);
	CHECK(zone_edit_remove(0, 0) == ZONE_EDIT_NO_ZONE);
	CHECK(zone_edit_move(0, 0, 1) == ZONE_EDIT_NO_ZONE);
	CHECK(zone_edit_rename(0, "Mine") == ZONE_EDIT_NO_ZONE);
	CHECK(zone_edit_copy(0, NULL) == ZONE_EDIT_NO_ZONE);
	CHECK(zone_edit_delete(0) == ZONE_EDIT_NO_ZONE);
	CHECK(codeplug_zone_count() == 1 && every_holds(0, (const uint16_t[]){ 1, 2, 3, 4, 5, 6 }, 6));
}

// Zones added go before it, and the saved zone follows it; deleting a zone moves it back
static void test_follows(void)
{
	fresh();
	set_saved_zone(0);			// on "All channels"
	uint16_t at = 99;
	zone_t local = zone_of("Local", (const uint16_t[]){ 3, 1 }, 2);
	CHECK(zone_edit_create(&local, &at) == ZONE_EDIT_OK && at == 0);
	CHECK(codeplug_zone_count() == 2 && codeplug_zone_stored_count() == 1 && codeplug_zones_stored());
	zone_t z;
	CHECK(codeplug_zone(0, &z) && !z.every && strcmp(z.name, "Local") == 0 && z.count == 2 &&
	      zone_channel(&z, 0) == 3);
	CHECK(every_holds(1, (const uint16_t[]){ 1, 2, 3, 4, 5, 6 }, 6));
	CHECK(saved_zone() == 1);		// still "All channels"

	zone_t club = zone_of("Club", (const uint16_t[]){ 6 }, 1);
	CHECK(zone_edit_create(&club, &at) == ZONE_EDIT_OK && at == 1);
	CHECK(codeplug_zone_count() == 3 && every_holds(2, (const uint16_t[]){ 1, 2, 3, 4, 5, 6 }, 6));
	CHECK(saved_zone() == 2);
	// A stored zone still edits as before
	CHECK(zone_edit_insert(1, 1, 2) == ZONE_EDIT_OK && codeplug_zone(1, &z) && z.count == 2);

	CHECK(zone_edit_delete(0) == ZONE_EDIT_OK);
	CHECK(codeplug_zone_count() == 2 && every_holds(1, (const uint16_t[]){ 1, 2, 3, 4, 5, 6 }, 6));
	CHECK(saved_zone() == 1);
	CHECK(zone_edit_delete(1) == ZONE_EDIT_NO_ZONE);	// the "All channels" zone stays
	CHECK(zone_edit_delete(0) == ZONE_EDIT_OK);	// the last stored zone: "All channels" alone again
	CHECK(codeplug_zone_count() == 1 && codeplug_zone_stored_count() == 0 && every_holds(0,
	      (const uint16_t[]){ 1, 2, 3, 4, 5, 6 }, 6));
	CHECK(saved_zone() == 0);
}

int main(void)
{
	test_builtin();
	test_walk();
	test_full();
	test_read_only();
	test_follows();
	if (failures) {
		printf("allChannelsZoneTest: %d failures\n", failures);
		return 1;
	}
	printf("allChannelsZoneTest: all passed\n");
	return 0;
}
