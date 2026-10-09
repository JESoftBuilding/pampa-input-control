#ifndef INPUT_CONTROL_CRYPTO_CHACHA20_POLY1305_H
#define INPUT_CONTROL_CRYPTO_CHACHA20_POLY1305_H
#include <cstdint>
#include <cstddef>
#include <cstring>

// AEAD_CHACHA20_POLY1305 de la RFC 8439: cifra y autentica en un solo paso.
//
// Por qué una implementación propia y no mbedTLS/BearSSL: la librería corre en ESP32, en
// ESP8266 y en la PC (tests nativos), y cada uno trae una biblioteca distinta o ninguna.
// Un solo código, sin dependencias, que se verifica en la PC contra los vectores oficiales
// de la RFC (test/test_crypto). NO es un algoritmo inventado: es el estándar, tal cual.
//
// Regla que el que llama NO puede romper: con una misma clave, un nonce no se usa dos
// veces. Repetirlo revela el XOR de los dos mensajes y permite falsificar. DeviceProtocol.h
// arma el nonce con (origen, época, secuencia) justamente para garantizarlo.
//
// Sin memoria dinámica. Aritmética de 32 bits (26 bits por limbo en Poly1305, al estilo
// poly1305-donna) y carga/descarga explícita de bytes: no depende del orden de bytes.
namespace input_control {
namespace crypto {

constexpr size_t KEY_LEN   = 32;
constexpr size_t NONCE_LEN = 12;
constexpr size_t TAG_LEN   = 16;

inline uint32_t load32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void store32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline void store64(uint8_t* p, uint64_t v) {
    store32(p, (uint32_t)v); store32(p + 4, (uint32_t)(v >> 32));
}
inline uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

// ── ChaCha20 (RFC 8439 §2.3) ─────────────────────────────────────────────────
inline void quarterRound(uint32_t* x, int a, int b, int c, int d) {
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 16);
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 12);
    x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 8);
    x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 7);
}

inline void chacha20Block(const uint8_t key[KEY_LEN], uint32_t counter,
                          const uint8_t nonce[NONCE_LEN], uint8_t out[64]) {
    uint32_t s[16];
    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;   // "expand 32-byte k"
    for (int i = 0; i < 8; ++i) s[4 + i] = load32(key + 4 * i);
    s[12] = counter;
    for (int i = 0; i < 3; ++i) s[13 + i] = load32(nonce + 4 * i);

    uint32_t x[16];
    for (int i = 0; i < 16; ++i) x[i] = s[i];
    for (int round = 0; round < 10; ++round) {
        quarterRound(x, 0, 4,  8, 12); quarterRound(x, 1, 5,  9, 13);
        quarterRound(x, 2, 6, 10, 14); quarterRound(x, 3, 7, 11, 15);
        quarterRound(x, 0, 5, 10, 15); quarterRound(x, 1, 6, 11, 12);
        quarterRound(x, 2, 7,  8, 13); quarterRound(x, 3, 4,  9, 14);
    }
    for (int i = 0; i < 16; ++i) store32(out + 4 * i, x[i] + s[i]);
}

// XOR de `in` con el flujo de ChaCha20 a partir del bloque `counter`. Cifra y descifra.
inline void chacha20Xor(const uint8_t key[KEY_LEN], uint32_t counter,
                        const uint8_t nonce[NONCE_LEN],
                        const uint8_t* in, size_t len, uint8_t* out) {
    uint8_t block[64];
    size_t off = 0;
    while (off < len) {
        chacha20Block(key, counter++, nonce, block);
        const size_t n = (len - off < 64) ? (len - off) : 64;
        for (size_t i = 0; i < n; ++i) out[off + i] = in[off + i] ^ block[i];
        off += n;
    }
}

// ── Poly1305 (RFC 8439 §2.5) ─────────────────────────────────────────────────
class Poly1305 {
public:
    explicit Poly1305(const uint8_t key[KEY_LEN]) {
        // r se recorta ("clamp") según la norma; se guarda en 5 limbos de 26 bits.
        r_[0] = (load32(key + 0))       & 0x3ffffff;
        r_[1] = (load32(key + 3)  >> 2) & 0x3ffff03;
        r_[2] = (load32(key + 6)  >> 4) & 0x3ffc0ff;
        r_[3] = (load32(key + 9)  >> 6) & 0x3f03fff;
        r_[4] = (load32(key + 12) >> 8) & 0x00fffff;
        for (int i = 0; i < 5; ++i) h_[i] = 0;
        for (int i = 0; i < 4; ++i) pad_[i] = load32(key + 16 + 4 * i);
    }

    void update(const uint8_t* m, size_t len) {
        if (len == 0) return;
        if (leftover_) {
            size_t want = 16 - leftover_;
            if (want > len) want = len;
            std::memcpy(buf_ + leftover_, m, want);
            leftover_ += want; m += want; len -= want;
            if (leftover_ < 16) return;
            blocks(buf_, 16, false);
            leftover_ = 0;
        }
        if (len >= 16) {
            const size_t whole = len & ~(size_t)15;
            blocks(m, whole, false);
            m += whole; len -= whole;
        }
        if (len) {
            std::memcpy(buf_, m, len);
            leftover_ = len;
        }
    }

    void finish(uint8_t tag[TAG_LEN]) {
        if (leftover_) {
            size_t i = leftover_;
            buf_[i++] = 1;
            for (; i < 16; ++i) buf_[i] = 0;
            blocks(buf_, 16, true);
        }
        uint32_t h0 = h_[0], h1 = h_[1], h2 = h_[2], h3 = h_[3], h4 = h_[4];
        uint32_t c;
        // propagar acarreos
                     c = h1 >> 26; h1 &= 0x3ffffff;
        h2 += c;     c = h2 >> 26; h2 &= 0x3ffffff;
        h3 += c;     c = h3 >> 26; h3 &= 0x3ffffff;
        h4 += c;     c = h4 >> 26; h4 &= 0x3ffffff;
        h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
        h1 += c;
        // g = h - p (con p = 2^130 - 5): se calcula h + 5 - 2^130
        uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
        uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
        uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
        uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
        uint32_t g4 = h4 + c - (1UL << 26);
        // elegir h si h < p, g si h >= p, sin bifurcar
        uint32_t mask = (g4 >> 31) - 1;
        g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
        mask = ~mask;
        h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
        h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;
        // h mod 2^128, en 4 palabras de 32 bits
        h0 = (h0)       | (h1 << 26);
        h1 = (h1 >> 6)  | (h2 << 20);
        h2 = (h2 >> 12) | (h3 << 14);
        h3 = (h3 >> 18) | (h4 << 8);
        // tag = (h + s) mod 2^128
        uint64_t f;
        f = (uint64_t)h0 + pad_[0];             h0 = (uint32_t)f;
        f = (uint64_t)h1 + pad_[1] + (f >> 32); h1 = (uint32_t)f;
        f = (uint64_t)h2 + pad_[2] + (f >> 32); h2 = (uint32_t)f;
        f = (uint64_t)h3 + pad_[3] + (f >> 32); h3 = (uint32_t)f;
        store32(tag + 0, h0); store32(tag + 4, h1); store32(tag + 8, h2); store32(tag + 12, h3);
    }

private:
    void blocks(const uint8_t* m, size_t bytes, bool final) {
        const uint32_t hibit = final ? 0 : (1UL << 24);   // el bit 2^128 de cada bloque
        const uint32_t r0 = r_[0], r1 = r_[1], r2 = r_[2], r3 = r_[3], r4 = r_[4];
        const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
        uint32_t h0 = h_[0], h1 = h_[1], h2 = h_[2], h3 = h_[3], h4 = h_[4];
        while (bytes >= 16) {
            // h += m[i]
            h0 += (load32(m + 0))       & 0x3ffffff;
            h1 += (load32(m + 3)  >> 2) & 0x3ffffff;
            h2 += (load32(m + 6)  >> 4) & 0x3ffffff;
            h3 += (load32(m + 9)  >> 6) & 0x3ffffff;
            h4 += (load32(m + 12) >> 8) | hibit;
            // h *= r
            const uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
            uint64_t       d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
            uint64_t       d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
            uint64_t       d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
            uint64_t       d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;
            // reducción parcial mod 2^130 - 5
            uint32_t c;
                      c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff;
            d1 += c;  c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff;
            d2 += c;  c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff;
            d3 += c;  c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff;
            d4 += c;  c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff;
            h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
            h1 += c;
            m += 16; bytes -= 16;
        }
        h_[0] = h0; h_[1] = h1; h_[2] = h2; h_[3] = h3; h_[4] = h4;
    }

    uint32_t r_[5];
    uint32_t h_[5];
    uint32_t pad_[4];
    uint8_t  buf_[16];
    size_t   leftover_ = 0;
};

inline void poly1305(const uint8_t key[KEY_LEN], const uint8_t* msg, size_t len,
                     uint8_t tag[TAG_LEN]) {
    Poly1305 p(key);
    p.update(msg, len);
    p.finish(tag);
}

// ── AEAD (RFC 8439 §2.8) ─────────────────────────────────────────────────────
// La etiqueta cubre: aad | relleno a 16 | cifrado | relleno a 16 | len(aad) | len(cifrado).
inline void aeadTag(const uint8_t key[KEY_LEN], const uint8_t nonce[NONCE_LEN],
                    const uint8_t* aad, size_t aad_len,
                    const uint8_t* ct, size_t len, uint8_t tag[TAG_LEN]) {
    uint8_t block0[64];
    chacha20Block(key, 0, nonce, block0);   // los primeros 32 bytes son la clave de un solo uso
    Poly1305 p(block0);
    const uint8_t zeros[16] = {0};
    p.update(aad, aad_len);
    if (aad_len % 16) p.update(zeros, 16 - (aad_len % 16));
    p.update(ct, len);
    if (len % 16) p.update(zeros, 16 - (len % 16));
    uint8_t lens[16];
    store64(lens, (uint64_t)aad_len);
    store64(lens + 8, (uint64_t)len);
    p.update(lens, 16);
    p.finish(tag);
}

// Cifra `pt` en `ct` (mismo largo) y deja la etiqueta en `tag`. `aad` va en claro pero
// queda autenticado: si alguien lo cambia, aeadOpen falla.
inline void aeadSeal(const uint8_t key[KEY_LEN], const uint8_t nonce[NONCE_LEN],
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* pt, size_t len, uint8_t* ct, uint8_t tag[TAG_LEN]) {
    if (len) chacha20Xor(key, 1, nonce, pt, len, ct);
    aeadTag(key, nonce, aad, aad_len, ct, len, tag);
}

// true y el texto en `pt` sólo si la etiqueta es válida. Si falla, `pt` no se toca.
inline bool aeadOpen(const uint8_t key[KEY_LEN], const uint8_t nonce[NONCE_LEN],
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* ct, size_t len, const uint8_t tag[TAG_LEN], uint8_t* pt) {
    uint8_t expected[TAG_LEN];
    aeadTag(key, nonce, aad, aad_len, ct, len, expected);
    uint8_t diff = 0;                        // comparación en tiempo constante
    for (size_t i = 0; i < TAG_LEN; ++i) diff |= (uint8_t)(expected[i] ^ tag[i]);
    if (diff != 0) return false;
    if (len) chacha20Xor(key, 1, nonce, ct, len, pt);
    return true;
}

} // namespace crypto
} // namespace input_control
#endif
