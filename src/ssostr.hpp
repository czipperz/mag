#pragma once

#include <limits.h>
#include <string.h>
#include <cz/allocator.hpp>
#include <cz/str.hpp>
#include <new>

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
            _len = ((str.len << 1) | 1);
        } else {
            _len = (str.len | ~LEN_MASK);
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
        _buffer[str.len] = 0;
        set_len(str.len);
    }

    const char* buffer() const { return _buffer; }

    void set_len(size_t len) { _buffer[MAX] = (char)(len << 1); }
    size_t len() const { return _buffer[MAX] >> 1; }

    bool is_short() const { return !(_buffer[MAX] & 1); }
};

}

/// Short String Optimized Str (const & non-owning String slice).  Useful for strings
/// where you expect the length to be < 16 bytes because it saves space and an allocation.
///
/// If the string is short, the first 15 bytes are the value and the
/// last byte is the 7 bits of the length and 1 bit the value 0.
/// This allows inline strings to be null terminated.
struct SSOStr {
    constexpr static const size_t MAX_SHORT_LEN = impl::ShortStr::MAX;

    union {
        impl::AllocatedStr allocated;
        impl::ShortStr short_;
    };

    /// Construct as a reference to `str`.  May maintain
    /// a pointer to `str.buffer`.  Never allocates.
    static SSOStr from_constant(cz::Str str) {
        SSOStr self;
        if (str.len <= impl::ShortStr::MAX) {
            new (&self.short_) impl::ShortStr;
            self.short_.init(str);
        } else {
            new (&self.allocated) impl::AllocatedStr;
            self.allocated.init(str);
        }
        return self;
    }

    /// Construct representing the character `c`.  Never allocates.
    static SSOStr from_char(char c) {
        SSOStr self;
        new (&self.short_) impl::ShortStr;
        self.short_.init({&c, 1});
        return self;
    }

    /// Construct using `str` as a basis.  If `str` doesn't fit inline, clones it using `allocator`.
    static SSOStr as_duplicate(cz::Allocator allocator, cz::Str str) {
        SSOStr self;
        if (str.len <= impl::ShortStr::MAX) {
            new (&self.short_) impl::ShortStr;
            self.short_.init(str);
        } else {
            char* buffer = (char*)allocator.alloc({str.len, 1});
            memcpy(buffer, str.buffer, str.len);
            new (&self.allocated) impl::AllocatedStr;
            self.allocated.init({buffer, str.len});
        }
        return self;
    }

    /// Deallocates if the string is out of line.
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

    SSOStr clone(cz::Allocator allocator) const {
        return SSOStr::as_duplicate(allocator, as_str());
    }
};

}
