#pragma once

#include <limits.h>
#include <string.h>
#include <cz/allocator.hpp>
#include <cz/str.hpp>

namespace mag {

namespace impl {

inline bool is_big_endian() {
    int x = 1;
    char* ptr = (char*)&x;
    return *ptr == 0;
}

struct AllocatedStr {
    constexpr static const size_t LEN_MASK = (((size_t)1 << ((sizeof(size_t) - 1) * CHAR_BIT)) - 1);

    const char* _buffer;
    size_t _len;

    void init(cz::Str str) {
        _buffer = str.buffer;
        if (is_big_endian()) {
            _len = (str.len << 1);
        } else {
            _len = (str.len & LEN_MASK);
        }
    }

    void drop(cz::Allocator allocator) { allocator.dealloc({(char*)_buffer, _len}); }

    const char* buffer() const { return _buffer; }

    size_t len() const {
        if (is_big_endian()) {
            return _len >> 1;
        } else {
            return (_len & LEN_MASK);
        }
    }
};

struct ShortStr {
    constexpr static const size_t MAX = sizeof(AllocatedStr) - 1;

    char _buffer[sizeof(AllocatedStr)];

    void init(cz::Str str) {
        memcpy(_buffer, str.buffer, str.len);
        set_len(str.len);
    }

    const char* buffer() const { return _buffer; }

    void set_len(size_t len) { _buffer[MAX] = ((len << 1) | 1); }
    size_t len() const { return _buffer[MAX] >> 1; }

    bool is_short() const { return _buffer[MAX] & 1; }
};

}

struct SSOStr {
    constexpr static const size_t MAX_SHORT_LEN = impl::ShortStr::MAX;

    union {
        impl::AllocatedStr allocated;
        impl::ShortStr short_;
    };

    void init_from_constant(cz::Str str) {
        if (str.len <= impl::ShortStr::MAX) {
            short_.init(str);
        } else {
            allocated.init(str);
        }
    }

    void init_char(char c) { short_.init({&c, 1}); }

    void init_duplicate(cz::Allocator allocator, cz::Str str) {
        if (str.len <= impl::ShortStr::MAX) {
            short_.init(str);
        } else {
            char* buffer = (char*)allocator.alloc({str.len, 1});
            memcpy(buffer, str.buffer, str.len);
            allocated.init({buffer, str.len});
        }
    }

    void drop(cz::Allocator allocator) {
        if (!is_short()) {
            allocated.drop(allocator);
        }
    }

    bool is_short() const { return short_.is_short(); }

    const char* buffer() const {
        if (is_short()) {
            return short_.buffer();
        } else {
            return allocated.buffer();
        }
    }

    size_t len() const {
        if (is_short()) {
            return short_.len();
        } else {
            return allocated.len();
        }
    }

    cz::Str as_str() const {
        if (is_short()) {
            return {short_.buffer(), short_.len()};
        } else {
            return {allocated.buffer(), allocated.len()};
        }
    }

    SSOStr duplicate(cz::Allocator allocator) const {
        SSOStr dup;
        dup.init_duplicate(allocator, as_str());
        return dup;
    }
};

}
