/**
 *  Copyright (C) 2015 Topology LP
 *  All rights reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

#ifndef CPPCODEC_DETAIL_STREAM_CODEC
#define CPPCODEC_DETAIL_STREAM_CODEC

#include <stdlib.h> // for abort()
#include <stdint.h>

#include "../parse_error.hpp"

namespace cppcodec {
namespace detail {

template <typename Codec, typename CodecVariant>
class stream_codec
{
public:
    template <typename Result, typename ResultState> static void encode(
            Result& encoded_result, ResultState&, const unsigned char* binary, size_t binary_size);

    template <typename Result, typename ResultState> static void decode(
            Result& binary_result, ResultState&, const char* encoded, size_t encoded_size);

    static constexpr size_t encoded_size(size_t binary_size) noexcept;
    static constexpr size_t decoded_max_size(size_t encoded_size) noexcept;
};

template <typename Codec, typename CodecVariant>
template <typename Result, typename ResultState>
inline void stream_codec<Codec, CodecVariant>::encode(
        Result& encoded_result, ResultState& state,
        const unsigned char* src, size_t src_size)
{
    const unsigned char* src_end = src + src_size - Codec::binary_block_size();

    for (; src <= src_end; src += Codec::binary_block_size()) {
        Codec::encode_block(encoded_result, state, src);
    }
    src_end += Codec::binary_block_size();

    if (src_end > src) {
        auto remaining_src_len = src_end - src;
        if (!remaining_src_len || remaining_src_len >= Codec::binary_block_size()) {
            abort();
            return;
        }
        Codec::encode_tail(encoded_result, state, src, remaining_src_len);
        Codec::pad(encoded_result, state, remaining_src_len);
    }
}

template <typename Codec, typename CodecVariant>
template <typename Result, typename ResultState>
inline void stream_codec<Codec, CodecVariant>::decode(
        Result& binary_result, ResultState& state,
        const char* src_encoded, size_t src_size)
{
    const char* src = src_encoded;
    const char* src_end = src + src_size;

    using V = CodecVariant;

    uint8_t idx[Codec::encoded_block_size()] = {};
    uint8_t last_value_idx = 0;

    while (src < src_end) {
        if (CodecVariant::should_ignore(idx[last_value_idx] = CodecVariant::index_of(*(src++)))) {
            continue;
        }
        if (CodecVariant::is_special_character(idx[last_value_idx])) {
            break;
        }

        ++last_value_idx;
        if (last_value_idx == Codec::encoded_block_size()) {
            Codec::decode_block(binary_result, state, idx);
            last_value_idx = 0;
        }
    }

    uint8_t last_idx = last_value_idx;
    if (CodecVariant::is_padding_symbol(idx[last_value_idx])) {
        // We're in here because we just read a (first) padding character. Try to read more.
        ++last_idx;
        while (src < src_end) {
            if (CodecVariant::is_eof(idx[last_idx] = CodecVariant::index_of(*(src++)))) {
                break;
            }
            if (!CodecVariant::is_padding_symbol(idx[last_idx])) {
                throw padding_error();
            }

            ++last_idx;
            if (last_idx > Codec::encoded_block_size()) {
                throw padding_error();
            }
        }
    }

    if (last_value_idx)  {
        if (CodecVariant::requires_padding() && last_idx != Codec::encoded_block_size()) {
            // If the input is not a multiple of the block size then the input is incorrect.
            throw padding_error();
        }
        if (last_value_idx >= Codec::encoded_block_size()) {
            abort();
            return;
        }
        Codec::decode_tail(binary_result, state, idx, last_value_idx);
    }
}

template <typename Codec, typename CodecVariant>
inline constexpr size_t stream_codec<Codec, CodecVariant>::encoded_size(size_t binary_size) noexcept
{
    using C = Codec;

    // constexpr rules make this a lot harder to read than it actually is.
    return CodecVariant::generates_padding()
            // With padding, the encoded size is a multiple of the encoded block size.
            // To calculate that, round the binary size up to multiple of the binary block size,
            // then convert to encoded by multiplying with { base32: 8/5, base64: 4/3 }.
            ? (binary_size + (C::binary_block_size() - 1)
                    - ((binary_size + (C::binary_block_size() - 1)) % C::binary_block_size()))
                    * C::encoded_block_size() / C::binary_block_size()
            // No padding: only pad to the next multiple of 5 bits, i.e. at most a single extra byte.
            : (binary_size * C::encoded_block_size() / C::binary_block_size())
                    + (((binary_size * C::encoded_block_size()) % C::binary_block_size()) ? 1 : 0);
}

template <typename Codec, typename CodecVariant>
inline constexpr size_t stream_codec<Codec, CodecVariant>::decoded_max_size(size_t encoded_size) noexcept
{
    using C = Codec;

    return CodecVariant::requires_padding()
            ? encoded_size * C::binary_block_size() / C::encoded_block_size()
            : (encoded_size * C::binary_block_size() / C::encoded_block_size())
                    + (((encoded_size * C::binary_block_size()) % C::encoded_block_size()) ? 1 : 0);
}

} // namespace detail
} // namespace cppcodec

#endif // CPPCODEC_DETAIL_STREAM_CODEC
