#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool aes128_ctr(const uint8_t key[16], const uint8_t initial_counter[16], const uint8_t* input, uint8_t* output,
                size_t length);
