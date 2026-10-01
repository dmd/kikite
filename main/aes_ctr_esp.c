#include <string.h>
#include "aes/esp_aes.h"
#include "aes_ctr.h"

bool aes128_ctr(const uint8_t key[16], const uint8_t initial_counter[16], const uint8_t* input, uint8_t* output,
                size_t length) {
    esp_aes_context context;
    unsigned char   counter[16];
    unsigned char   stream_block[16];
    size_t          offset = 0;
    memcpy(counter, initial_counter, sizeof(counter));
    esp_aes_init(&context);
    bool ok = esp_aes_setkey(&context, key, 128) == 0 &&
              esp_aes_crypt_ctr(&context, length, &offset, counter, stream_block, input, output) == 0;
    esp_aes_free(&context);
    return ok;
}
