#include <openssl/evp.h>
#include "aes_ctr.h"

bool aes128_ctr(const uint8_t key[16], const uint8_t initial_counter[16], const uint8_t* input, uint8_t* output,
                size_t length) {
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    int             written = 0;
    int             final   = 0;
    bool ok = context && EVP_EncryptInit_ex(context, EVP_aes_128_ctr(), NULL, key, initial_counter) == 1 &&
              EVP_EncryptUpdate(context, output, &written, input, (int)length) == 1 &&
              EVP_EncryptFinal_ex(context, output + written, &final) == 1;
    EVP_CIPHER_CTX_free(context);
    return ok;
}
