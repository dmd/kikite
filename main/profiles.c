#include "profiles.h"
#include <string.h>
#include "meshtastic_slot.h"

#define SYNC_PRIVATE    0x12
#define SYNC_LORAWAN    0x34
#define SYNC_MESHTASTIC 0x2B

static const sweep_range_t us915_ranges[] = {
    {"US915 902-928", 902000000, 928000000},
    {"LoRaWAN US uplink 902-915", 902000000, 915000000},
    {"LoRaWAN US downlink 923-928", 923000000, 928000000},
    {"Wide 860-930", 860000000, 930000000},
};

static const sweep_range_t eu868_ranges[] = {
    {"EU868 863-870", 863000000, 870000000},
    {"Wide 860-930", 860000000, 930000000},
    {"US915 902-928", 902000000, 928000000},
};

static const sweep_range_t band433_ranges[] = {
    {"EU433 433-435", 433000000, 435000000},
    {"Wide 410-493", 410000000, 493000000},
};

static listen_channel_t channel(uint32_t frequency_hz, uint8_t sf, uint16_t bw_khz, uint8_t cr, uint8_t sync,
                                uint16_t preamble, bool invert_iq) {
    return (listen_channel_t){
        .frequency_hz     = frequency_hz,
        .spreading_factor = sf,
        .bandwidth_khz    = bw_khz,
        .coding_rate      = cr,
        .sync_word        = sync,
        .preamble_length  = preamble,
        .invert_iq        = invert_iq,
    };
}

static void single(listen_params_t* out, listen_channel_t only) {
    memset(out, 0, sizeof(*out));
    out->channels[0]   = only;
    out->channel_count = 1;
}

static void hop(listen_params_t* out, const uint32_t* frequencies, uint8_t frequency_count, uint8_t sf_low,
                uint8_t sf_high, uint16_t bw_khz, bool downlink, uint32_t dwell_ms) {
    memset(out, 0, sizeof(*out));
    for (uint8_t sf = sf_low; sf <= sf_high; sf++) {
        for (uint8_t i = 0; i < frequency_count && out->channel_count < RADIO_MAX_HOP_CHANNELS; i++) {
            out->channels[out->channel_count++] =
                channel(frequencies[i], sf, bw_khz, 5, SYNC_LORAWAN, 8, downlink);
        }
    }
    out->dwell_ms = dwell_ms;
}

static void meshtastic(listen_params_t* out, uint32_t region_start_hz, uint32_t region_end_hz, const char* preset,
                       uint8_t sf, uint16_t bw_khz, uint8_t cr) {
    uint32_t frequency = meshtastic_default_frequency(region_start_hz, region_end_hz, bw_khz, preset);
    single(out, channel(frequency, sf, bw_khz, cr, SYNC_MESHTASTIC, 16, false));
}

static uint8_t meshcore_preamble(uint8_t sf) {
    return sf <= 8 ? 32 : 16;
}

static void us_meshtastic_long_fast(listen_params_t* out, uint32_t cursor_hz) {
    meshtastic(out, 902000000, 928000000, "LongFast", 11, 250, 5);
}

static void us_meshtastic_medium_fast(listen_params_t* out, uint32_t cursor_hz) {
    meshtastic(out, 902000000, 928000000, "MediumFast", 9, 250, 5);
}

static void us_meshcore(listen_params_t* out, uint32_t cursor_hz) {
    single(out, channel(910525000, 7, 62, 5, SYNC_PRIVATE, meshcore_preamble(7), false));
}

static const uint32_t us_subband2_uplinks[] = {903900000, 904100000, 904300000, 904500000,
                                               904700000, 904900000, 905100000, 905300000};
static const uint32_t us_downlinks[]        = {923300000, 923900000, 924500000, 925100000,
                                               925700000, 926300000, 926900000, 927500000};

static void us_lorawan_up_sf7(listen_params_t* out, uint32_t cursor_hz) {
    hop(out, us_subband2_uplinks, 8, 7, 7, 125, false, 2000);
}

static void us_lorawan_up_all(listen_params_t* out, uint32_t cursor_hz) {
    hop(out, us_subband2_uplinks, 8, 7, 10, 125, false, 1500);
}

static void us_lorawan_down(listen_params_t* out, uint32_t cursor_hz) {
    hop(out, us_downlinks, 8, 7, 10, 500, true, 1500);
}

static void eu_meshtastic_long_fast(listen_params_t* out, uint32_t cursor_hz) {
    meshtastic(out, 869400000, 869650000, "LongFast", 11, 250, 5);
}

static void eu_meshcore_narrow(listen_params_t* out, uint32_t cursor_hz) {
    single(out, channel(869618000, 8, 62, 8, SYNC_PRIVATE, meshcore_preamble(8), false));
}

static const uint32_t eu_uplinks[]  = {868100000, 868300000, 868500000, 867100000,
                                       867300000, 867500000, 867700000, 867900000};

static void eu_lorawan_up_sf7(listen_params_t* out, uint32_t cursor_hz) {
    hop(out, eu_uplinks, 8, 7, 7, 125, false, 2000);
}

static void eu_lorawan_up_default_all(listen_params_t* out, uint32_t cursor_hz) {
    hop(out, eu_uplinks, 3, 7, 12, 125, false, 1500);
}

static void eu_lorawan_rx2(listen_params_t* out, uint32_t cursor_hz) {
    single(out, channel(869525000, 9, 125, 5, SYNC_LORAWAN, 8, true));
}

static void band433_meshtastic(listen_params_t* out, uint32_t cursor_hz) {
    meshtastic(out, 433000000, 434000000, "LongFast", 11, 250, 5);
}

static void band433_meshcore_narrow(listen_params_t* out, uint32_t cursor_hz) {
    single(out, channel(433650000, 8, 62, 5, SYNC_PRIVATE, meshcore_preamble(8), false));
}

static void band433_meshcore_wide(listen_params_t* out, uint32_t cursor_hz) {
    single(out, channel(433650000, 11, 250, 5, SYNC_PRIVATE, meshcore_preamble(11), false));
}

static const uint32_t eu433_uplinks[] = {433175000, 433375000, 433575000};

static void band433_lorawan_up(listen_params_t* out, uint32_t cursor_hz) {
    hop(out, eu433_uplinks, 3, 7, 12, 125, false, 1500);
}

static const listen_profile_t us915_profiles[] = {
    {"Meshtastic LongFast (US)", us_meshtastic_long_fast},
    {"Meshtastic MediumFast (US)", us_meshtastic_medium_fast},
    {"MeshCore USA", us_meshcore},
    {"LoRaWAN uplinks, sub-band 2, SF7", us_lorawan_up_sf7},
    {"LoRaWAN uplinks, sub-band 2, SF7-10", us_lorawan_up_all},
    {"LoRaWAN downlinks, SF7-10", us_lorawan_down},
};

static const listen_profile_t eu868_profiles[] = {
    {"Meshtastic LongFast (EU)", eu_meshtastic_long_fast},
    {"MeshCore EU/UK narrow", eu_meshcore_narrow},
    {"LoRaWAN uplinks, 8 channels, SF7", eu_lorawan_up_sf7},
    {"LoRaWAN uplinks, 3 channels, SF7-12", eu_lorawan_up_default_all},
    {"LoRaWAN downlinks, RX2 869.525 SF9", eu_lorawan_rx2},
};

static const listen_profile_t band433_profiles[] = {
    {"Meshtastic LongFast (433)", band433_meshtastic},
    {"MeshCore 433 narrow", band433_meshcore_narrow},
    {"MeshCore 433 wide", band433_meshcore_wide},
    {"LoRaWAN EU433 uplinks, SF7-12", band433_lorawan_up},
};

#define COUNT(array) ((uint8_t)(sizeof(array) / sizeof((array)[0])))

const char* profiles_region_name(region_t region) {
    switch (region) {
        case REGION_US915:
            return "US 915 MHz";
        case REGION_EU868:
            return "EU 868 MHz";
        case REGION_433:
            return "433 MHz";
        default:
            return "?";
    }
}

const sweep_range_t* profiles_sweep_ranges(region_t region, uint8_t* out_count) {
    switch (region) {
        case REGION_EU868:
            *out_count = COUNT(eu868_ranges);
            return eu868_ranges;
        case REGION_433:
            *out_count = COUNT(band433_ranges);
            return band433_ranges;
        case REGION_US915:
        default:
            *out_count = COUNT(us915_ranges);
            return us915_ranges;
    }
}

const listen_profile_t* profiles_listen(region_t region, uint8_t* out_count) {
    switch (region) {
        case REGION_EU868:
            *out_count = COUNT(eu868_profiles);
            return eu868_profiles;
        case REGION_433:
            *out_count = COUNT(band433_profiles);
            return band433_profiles;
        case REGION_US915:
        default:
            *out_count = COUNT(us915_profiles);
            return us915_profiles;
    }
}
