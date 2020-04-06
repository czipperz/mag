#include "contents.hpp"

#include <string.h>
#include <Tracy.hpp>
#include <cz/heap.hpp>
#include <cz/util.hpp>
#include "ssostr.hpp"

namespace mag {

#define CONTENTS_BUCKET_MAX_SIZE 512
#define CONTENTS_BUCKET_DESIRED_LEN (CONTENTS_BUCKET_MAX_SIZE * 3 / 4)

void Contents::drop() {
    for (size_t i = 0; i < buckets.len(); ++i) {
        cz::heap_allocator().dealloc({buckets[i].elems, CONTENTS_BUCKET_MAX_SIZE});
    }
    buckets.drop(cz::heap_allocator());
}

static cz::Slice<char> bucket_alloc() {
    char* buffer = (char*)cz::heap_allocator().alloc({CONTENTS_BUCKET_MAX_SIZE, 1});
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
    CZ_DEBUG_ASSERT(start + len <= this->len);
    this->len -= len;

    for (size_t v = 0; v < buckets.len(); ++v) {
        if (start < buckets[v].len) {
            if (start + len <= buckets[v].len) {
                bucket_remove(&buckets[v], start, len);
                return;
            } else {
                // :EmptyBuckets This is where empty buckets are created, and
                // then never reclaimed.  Perhaps we sort them to the end of the
                // list periodically and then reuse them that way?
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
    CZ_DEBUG_ASSERT(start <= this->len);
    this->len += str.len;

    for (size_t b = 0; b < buckets.len(); ++b) {
        if (start <= buckets[b].len) {
            if (buckets[b].len + str.len <= CONTENTS_BUCKET_MAX_SIZE) {
                bucket_insert(&buckets[b], start, str);
                return;
            } else {
                // Overflowing one buffer into multiple buffers.
                size_t extra_buffers = (buckets[b].len + str.len - 1) / CONTENTS_BUCKET_DESIRED_LEN;
                buckets.reserve(cz::heap_allocator(), extra_buffers);
                for (size_t i = 0; i < extra_buffers; ++i) {
                    buckets.insert(b + i + 1, bucket_alloc());
                }

                // Characters after the start point are saved for later
                char overflow[CONTENTS_BUCKET_MAX_SIZE];
                size_t overflow_offset = start;
                size_t overflow_len = buckets[b].len - overflow_offset;
                memcpy(overflow, buckets[b].elems + overflow_offset, overflow_len);
                buckets[b].len = overflow_offset;
                size_t overflow_index = 0;

                // Overflow the initial buffer into the second
                if (buckets[b].len > CONTENTS_BUCKET_DESIRED_LEN) {
                    bucket_append(&buckets[b + 1], {buckets[b].elems + CONTENTS_BUCKET_DESIRED_LEN,
                                                    buckets[b].len - CONTENTS_BUCKET_DESIRED_LEN});
                    buckets[b].len = CONTENTS_BUCKET_DESIRED_LEN;
                }

                // Fill buffers (including initial) except for the last one
                size_t str_index = 0;
                for (size_t bucket_index = b; bucket_index < b + extra_buffers; ++bucket_index) {
                    size_t offset = CONTENTS_BUCKET_DESIRED_LEN - buckets[bucket_index].len;
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
        buckets.reserve(cz::heap_allocator(),
                        (str.len + CONTENTS_BUCKET_DESIRED_LEN - 1) / CONTENTS_BUCKET_DESIRED_LEN);
        do {
            cz::Slice<char> bucket = bucket_alloc();
            if (str.len > CONTENTS_BUCKET_DESIRED_LEN) {
                bucket_append(&bucket, {str.buffer, CONTENTS_BUCKET_DESIRED_LEN});
                str.buffer += CONTENTS_BUCKET_DESIRED_LEN;
                str.len -= CONTENTS_BUCKET_DESIRED_LEN;
            } else {
                bucket_append(&bucket, {str.buffer, str.len});
                str.len = 0;
            }
            buckets.push(bucket);
        } while (str.len > 0);
    }
}

void Contents::append(cz::Str str) {
    insert(len, str);
}

static void slice_impl(char* buffer,
                       cz::Slice<const cz::Slice<char>> buckets,
                       Contents_Iterator start,
                       uint64_t len) {
    uint64_t bucket_index = start.index;
    for (size_t bucket = start.bucket; bucket < buckets.len; ++bucket) {
        if (bucket_index + len <= buckets[bucket].len) {
            memcpy(buffer, buckets[bucket].elems + bucket_index, len);
            return;
        } else {
            size_t offset = buckets[bucket].len - bucket_index;
            memcpy(buffer, buckets[bucket].elems + bucket_index, offset);
            buffer += offset;
            len -= offset;
            bucket_index = 0;
        }
    }
}

void Contents::stringify_into(cz::Allocator allocator, cz::String* string) const {
    string->reserve(allocator, len);
    slice_impl(string->buffer(), buckets, start(), len);
    string->set_len(string->len() + len);
}

cz::String Contents::stringify(cz::Allocator allocator) const {
    cz::String string = {};
    stringify_into(allocator, &string);
    return string;
}

SSOStr Contents::slice(cz::Allocator allocator, Contents_Iterator start, uint64_t end) const {
    ZoneScoped;

    CZ_DEBUG_ASSERT(start.position <= end);
    CZ_DEBUG_ASSERT(end <= len);

    SSOStr value;
    uint64_t len = end - start.position;
    if (len > SSOStr::MAX_SHORT_LEN) {
        char* buffer = (char*)allocator.alloc({len, 1});
        slice_impl(buffer, buckets, start, len);
        value.allocated.init({buffer, len});
    } else {
        char buffer[SSOStr::MAX_SHORT_LEN];
        slice_impl(buffer, buckets, start, len);
        value.short_.init({buffer, len});
    }
    return value;
}

void Contents::slice_into(Contents_Iterator start, uint64_t end, char* string) const {
    slice_impl(string, buckets, start, end - start.position);
}

char Contents::get_once(uint64_t pos) const {
    ZoneScoped;

    for (size_t i = 0; i < buckets.len(); ++i) {
        if (pos < buckets[i].len) {
            return buckets[i][pos];
        }
        pos -= buckets[i].len;
    }

    CZ_PANIC("Out of bounds");
}

Contents_Iterator Contents::iterator_at(uint64_t pos) const {
    ZoneScoped;

    Contents_Iterator it;
    it.contents = this;
    it.position = pos;

    for (size_t i = 0; i < buckets.len(); ++i) {
        if (pos < buckets[i].len) {
            it.bucket = i;
            it.index = pos;
            return it;
        }
        pos -= buckets[i].len;
    }

    it.bucket = buckets.len();
    it.index = 0;
    return it;
}

void Contents_Iterator::retreat(uint64_t offset) {
    ZoneScoped;

    CZ_DEBUG_ASSERT(position >= offset);
    position -= offset;
    if (offset > index) {
        offset -= index;
        while (1) {
            CZ_DEBUG_ASSERT(bucket > 0);
            --bucket;
            if (offset <= contents->buckets[bucket].len) {
                break;
            }
            offset -= contents->buckets[bucket].len;
        }
        index = contents->buckets[bucket].len - offset;
    } else {
        index -= offset;
    }
}

void Contents_Iterator::advance(uint64_t offset) {
    ZoneScoped;

    CZ_DEBUG_ASSERT(position + offset <= contents->len);
    position += offset;
    index += offset;
    while (index >= contents->buckets[bucket].len) {
        if (bucket == contents->buckets.len()) {
            CZ_DEBUG_ASSERT(index == contents->buckets[bucket].len);
            break;
        }

        index -= contents->buckets[bucket].len;
        ++bucket;
        if (bucket == contents->buckets.len()) {
            CZ_DEBUG_ASSERT(index == 0);
            break;
        }
    }
}

}
