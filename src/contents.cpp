#include "contents.hpp"

#include <string.h>
#include <cz/heap.hpp>
#include <cz/util.hpp>
#include "ssostr.hpp"

namespace mag {

#define MAX 512
#define DESIRED_LEN (MAX * 3 / 4)

void Contents::drop() {
    for (size_t i = 0; i < buckets.len(); ++i) {
        cz::heap_allocator().dealloc({buckets[i].elems, MAX});
    }
    buckets.drop(cz::heap_allocator());
}

static cz::Slice<char> bucket_alloc() {
    char* buffer = (char*)cz::heap_allocator().alloc({MAX, 1}).buffer;
    return {buffer, 0};
}

static void bucket_remove(cz::Slice<char>* bucket, uint64_t start, uint64_t len) {
    uint64_t end = start + len;
    memmove(bucket->elems + start, bucket->elems + end, bucket->len - end);
    bucket->len -= len;
}

static void bucket_insert(cz::Slice<char>* bucket, uint64_t position, cz::Str str) {
    memmove(bucket->elems + position + str.len, bucket->elems + position, bucket->len - position);
    memcpy(bucket->elems + position, str.buffer, str.len);
    bucket->len += str.len;
}

static void bucket_append(cz::Slice<char>* bucket, cz::Str str) {
    memcpy(bucket->elems + bucket->len, str.buffer, str.len);
    bucket->len += str.len;
}

void Contents::remove(uint64_t start, uint64_t len) {
    for (size_t v = 0; v < buckets.len(); ++v) {
        if (start < buckets[v].len) {
            if (start + len <= buckets[v].len) {
                bucket_remove(&buckets[v], start, len);
                return;
            } else {
                // TODO: what do we do with empty buckets?
                len -= buckets[v].len - start;
                buckets[v].len = start;
                start = 0;
            }
        } else {
            start -= buckets[v].len;
        }
    }

    CZ_DEBUG_ASSERT(len == 0);
}

void Contents::insert(uint64_t start, cz::Str str) {
    for (size_t b = 0; b < buckets.len(); ++b) {
        if (start <= buckets[b].len) {
            if (buckets[b].len + str.len <= MAX) {
                bucket_insert(&buckets[b], start, str);
                return;
            } else {
                // Overflowing one buffer into multiple buffers.
                size_t extra_buffers = (buckets[b].len + str.len - 1) / DESIRED_LEN;
                buckets.reserve(cz::heap_allocator(), extra_buffers);
                for (size_t i = 0; i < extra_buffers; ++i) {
                    buckets.insert(b + i + 1, bucket_alloc());
                }

                // Characters after the start point are saved for later
                char overflow[MAX];
                size_t overflow_offset = start;
                size_t overflow_len = buckets[b].len - overflow_offset;
                memcpy(overflow, buckets[b].elems + overflow_offset, overflow_len);
                buckets[b].len = overflow_offset;
                size_t overflow_index = 0;

                // Overflow the initial buffer into the second
                if (buckets[b].len > DESIRED_LEN) {
                    bucket_append(&buckets[b + 1],
                                  {buckets[b].elems + DESIRED_LEN, buckets[b].len - DESIRED_LEN});
                    buckets[b].len = DESIRED_LEN;
                }

                // Fill buffers (including initial) except for the last one
                size_t str_index = 0;
                for (size_t bucket_index = b; bucket_index < b + extra_buffers; ++bucket_index) {
                    size_t offset = DESIRED_LEN - buckets[bucket_index].len;
                    if (str_index + offset > str.len) {
                        // Here we are inserting a small string into a big
                        // buffer so need to split the final string into two.
                        size_t len = str.len - str_index;
                        bucket_append(&buckets[bucket_index], {str.buffer + str_index, len});
                        overflow_index = offset - len;
                        bucket_append(&buckets[bucket_index], {overflow, overflow_index});
                        str_index = str.len;
                    } else {
                        bucket_append(&buckets[bucket_index], {str.buffer + str_index, offset});
                        str_index += offset;
                    }
                }

                // Fill final buffer
                bucket_append(&buckets[b + extra_buffers],
                              {str.buffer + str_index, str.len - str_index});
                bucket_append(&buckets[b + extra_buffers],
                              {overflow + overflow_index, overflow_len - overflow_index});
                return;
            }
        } else {
            start -= buckets[b].len;
        }
    }

    CZ_DEBUG_ASSERT(start == 0);
    if (str.len > 0) {
        buckets.reserve(cz::heap_allocator(), (str.len + DESIRED_LEN - 1) / DESIRED_LEN);
        do {
            cz::Slice<char> bucket = bucket_alloc();
            if (str.len > DESIRED_LEN) {
                bucket_append(&bucket, {str.buffer, DESIRED_LEN});
                str.buffer += DESIRED_LEN;
                str.len -= DESIRED_LEN;
            } else {
                bucket_append(&bucket, {str.buffer, str.len});
                str.len = 0;
            }
            buckets.push(bucket);
        } while (str.len > 0);
    }
}

static void slice_into(char* buffer,
                       cz::Slice<const cz::Slice<char>> buckets,
                       uint64_t start,
                       uint64_t len) {
    for (size_t i = 0; i < buckets.len; ++i) {
        if (start < buckets[i].len) {
            if (start + len < buckets[i].len) {
                memcpy(buffer, buckets[i].elems + start, len);
                return;
            } else {
                size_t offset = buckets[i].len - start;
                memcpy(buffer, buckets[i].elems + start, offset);
                buffer += offset;
                start = 0;
                len -= offset;
            }
        } else {
            start -= buckets[i].len;
        }
    }
}

cz::String Contents::stringify(cz::Allocator allocator) const {
    cz::String string = {};
    string.reserve(allocator, len());
    slice_into(string.buffer(), buckets, 0, string.cap());
    string.set_len(string.cap());
    return string;
}

SSOStr Contents::slice(cz::Allocator allocator, uint64_t start, uint64_t end) const {
    CZ_DEBUG_ASSERT(start <= end);
    CZ_DEBUG_ASSERT(end <= len());

    SSOStr value;
    uint64_t len = end - start;
    if (end > start + SSOStr::MAX_SHORT_LEN) {
        char* buffer = (char*)allocator.alloc({len, 1}).buffer;
        slice_into(buffer, buckets, start, len);
        value.allocated.init({buffer, len});
    } else {
        char buffer[SSOStr::MAX_SHORT_LEN];
        slice_into(buffer, buckets, start, len);
        value.short_.init({buffer, len});
    }
    return value;
}

char Contents::operator[](uint64_t pos) const {
    for (size_t i = 0; i < buckets.len(); ++i) {
        if (pos < buckets[i].len) {
            return buckets[i][pos];
        }
        pos -= buckets[i].len;
    }

    CZ_PANIC("Out of bounds");
}

uint64_t Contents::len() const {
    uint64_t sum = 0;
    for (size_t i = 0; i < buckets.len(); ++i) {
        sum += buckets[i].len;
    }
    return sum;
}

bool Contents::is_bucket_separator(uint64_t pos) const {
    for (size_t i = 0; i < buckets.len(); ++i) {
        if (pos < buckets[i].len) {
            return false;
        }
        if (pos == buckets[i].len) {
            return true;
        }
        pos -= buckets[i].len;
    }

    CZ_PANIC("Out of bounds");
}

void Contents::get_bucket(uint64_t pos, size_t* bucket, size_t* index) const {
    for (size_t i = 0; i < buckets.len(); ++i) {
        if (pos < buckets[i].len) {
            *bucket = i;
            *index = pos;
            return;
        }
        pos -= buckets[i].len;
    }

    *bucket = buckets.len();
    *index = 0;
}

}
