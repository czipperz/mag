#include "contents.hpp"

#include <string.h>
#include <Tracy.hpp>
#include <cz/heap.hpp>
#include <cz/util.hpp>
#include "movement.hpp"
#include "ssostr.hpp"

namespace mag {

#define CONTENTS_BUCKET_MAX_SIZE 512
#define CONTENTS_BUCKET_DESIRED_LEN (CONTENTS_BUCKET_MAX_SIZE * 3 / 4)

void Contents::drop() {
    for (size_t i = 0; i < buckets.len(); ++i) {
        cz::heap_allocator().dealloc(buckets[i].elems, CONTENTS_BUCKET_MAX_SIZE);
    }
    buckets.drop(cz::heap_allocator());
    bucket_lfs.drop(cz::heap_allocator());
}

static cz::Slice<char> bucket_alloc() {
    char* buffer = cz::heap_allocator().alloc<char>(CONTENTS_BUCKET_MAX_SIZE);
    return {buffer, 0};
}

static uint64_t count_lines(cz::Str str) {
    ZoneScoped;
    return str.count('\n');
}

static void bucket_remove(cz::Slice<char>* bucket, uint64_t* lines, uint64_t start, uint64_t len) {
    ZoneScoped;

    uint64_t count = count_lines({bucket->elems + start, len});
    CZ_DEBUG_ASSERT(*lines >= count);
    *lines -= count;

    uint64_t end = start + len;
    memmove(bucket->elems + start, bucket->elems + end, bucket->len - end);
    bucket->len -= len;
}

static void bucket_insert(cz::Slice<char>* bucket,
                          uint64_t* lines,
                          uint64_t position,
                          cz::Str str) {
    ZoneScoped;

    *lines += count_lines(str);

    memmove(bucket->elems + position + str.len, bucket->elems + position, bucket->len - position);
    memcpy(bucket->elems + position, str.buffer, str.len);
    bucket->len += str.len;
}

static void bucket_append(cz::Slice<char>* bucket, cz::Str str) {
    ZoneScoped;

    memcpy(bucket->elems + bucket->len, str.buffer, str.len);
    bucket->len += str.len;
}

static void bucket_append(cz::Slice<char>* bucket, uint64_t* lines, cz::Str str) {
    ZoneScoped;

    *lines += count_lines(str);
    memcpy(bucket->elems + bucket->len, str.buffer, str.len);
    bucket->len += str.len;
}

void Contents::remove(uint64_t start, uint64_t len) {
    ZoneScoped;
    CZ_DEBUG_ASSERT(start + len <= this->len);
    this->len -= len;

    for (size_t v = 0; v < buckets.len();) {
        if (start < buckets[v].len) {
            if (start + len <= buckets[v].len) {
                bucket_remove(&buckets[v], &bucket_lfs[v], start, len);

                // Remove empty buckets.
                if (buckets[v].len == 0) {
                    cz::heap_allocator().dealloc(buckets[v].elems, CONTENTS_BUCKET_MAX_SIZE);
                    buckets.remove(v);
                    bucket_lfs.remove(v);
                }

                return;
            } else {
                bucket_lfs[v] -= count_lines({buckets[v].elems + start, buckets[v].len - start});

                len -= buckets[v].len - start;
                buckets[v].len = start;
                start = 0;

                // Remove empty buckets.
                if (buckets[v].len == 0) {
                    cz::heap_allocator().dealloc(buckets[v].elems, CONTENTS_BUCKET_MAX_SIZE);
                    buckets.remove(v);
                    bucket_lfs.remove(v);
                } else {
                    ++v;
                }
            }
        } else {
            start -= buckets[v].len;
            ++v;
        }
    }

    CZ_DEBUG_ASSERT(len == 0);
}

static void insert_empty(Contents* contents, cz::Str str) {
    CZ_DEBUG_ASSERT(contents->buckets.len() == 0);

    if (str.len == 0) {
        return;
    }

    contents->len += str.len;

    size_t num_buckets = (str.len + CONTENTS_BUCKET_DESIRED_LEN - 1) / CONTENTS_BUCKET_DESIRED_LEN;
    contents->buckets.reserve(cz::heap_allocator(), num_buckets);
    contents->bucket_lfs.reserve(cz::heap_allocator(), num_buckets);
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
        contents->buckets.push(bucket);
        contents->bucket_lfs.push(count_lines({bucket.elems, bucket.len}));
    } while (str.len > 0);
}

static void insert_at(Contents* contents, Contents_Iterator iterator, cz::Str str) {
    if (!iterator.at_bob()) {
        // Go to the end of the previous buffer if we are at the start of a buffer.
        iterator.retreat();
        iterator.index++;
        iterator.position++;
    }

    size_t b = iterator.bucket;

    CZ_DEBUG_ASSERT(b < contents->buckets.len());
    CZ_DEBUG_ASSERT(iterator.index <= contents->buckets[b].len);

    contents->len += str.len;

    // If we can fit in the current bucket then we just insert into it.
    if (contents->buckets[b].len + str.len <= CONTENTS_BUCKET_MAX_SIZE) {
        bucket_insert(&contents->buckets[b], &contents->bucket_lfs[b], iterator.index, str);
    } else {
        // Overflowing one buffer into multiple buffers.
        size_t extra_buffers =
            (contents->buckets[b].len + str.len - 1) / CONTENTS_BUCKET_DESIRED_LEN;
        contents->buckets.reserve(cz::heap_allocator(), extra_buffers);
        contents->bucket_lfs.reserve(cz::heap_allocator(), extra_buffers);
        for (size_t i = 0; i < extra_buffers; ++i) {
            contents->buckets.insert(b + i + 1, bucket_alloc());
            contents->bucket_lfs.insert(b + i + 1, 0);
        }

        // Characters after the start point are saved for later
        char overflow[CONTENTS_BUCKET_MAX_SIZE];
        size_t overflow_offset = iterator.index;
        size_t overflow_len = contents->buckets[b].len - overflow_offset;
        uint64_t overflow_lines =
            count_lines({contents->buckets[b].elems + overflow_offset, overflow_len});
        memcpy(overflow, contents->buckets[b].elems + overflow_offset, overflow_len);
        contents->buckets[b].len = overflow_offset;
        contents->bucket_lfs[b] -= overflow_lines;
        size_t overflow_index = 0;

        // Overflow the initial buffer into the second
        if (contents->buckets[b].len > CONTENTS_BUCKET_DESIRED_LEN) {
            uint64_t lines = count_lines({contents->buckets[b].elems + CONTENTS_BUCKET_DESIRED_LEN,
                                          contents->buckets[b].len - CONTENTS_BUCKET_DESIRED_LEN});
            bucket_append(&contents->buckets[b + 1],
                          {contents->buckets[b].elems + CONTENTS_BUCKET_DESIRED_LEN,
                           contents->buckets[b].len - CONTENTS_BUCKET_DESIRED_LEN});
            contents->buckets[b].len = CONTENTS_BUCKET_DESIRED_LEN;
            contents->bucket_lfs[b + 1] += lines;
            contents->bucket_lfs[b] -= lines;
        }

        // Fill buffers (including initial) except for the last one
        size_t str_index = 0;
        for (size_t bucket_index = b; bucket_index < b + extra_buffers; ++bucket_index) {
            size_t offset = CONTENTS_BUCKET_DESIRED_LEN - contents->buckets[bucket_index].len;
            if (str_index + offset > str.len) {
                // Here we are inserting a small string into a big
                // buffer so need to split the final string into two.
                size_t len = str.len - str_index;
                bucket_append(&contents->buckets[bucket_index], &contents->bucket_lfs[bucket_index],
                              {str.buffer + str_index, len});
                overflow_index = offset - len;
                bucket_append(&contents->buckets[bucket_index], &contents->bucket_lfs[bucket_index],
                              {overflow, overflow_index});
                str_index = str.len;
            } else {
                bucket_append(&contents->buckets[bucket_index], &contents->bucket_lfs[bucket_index],
                              {str.buffer + str_index, offset});
                str_index += offset;
            }
        }

        // Fill final buffer
        bucket_append(&contents->buckets[b + extra_buffers],
                      &contents->bucket_lfs[b + extra_buffers],
                      {str.buffer + str_index, str.len - str_index});
        bucket_append(&contents->buckets[b + extra_buffers],
                      &contents->bucket_lfs[b + extra_buffers],
                      {overflow + overflow_index, overflow_len - overflow_index});
    }
}

void Contents::insert(uint64_t start, cz::Str str) {
    ZoneScoped;

    if (buckets.len() == 0) {
        CZ_DEBUG_ASSERT(start == 0);
        CZ_DEBUG_ASSERT(len == 0);
        insert_empty(this, str);
    } else {
        insert_at(this, iterator_at(start), str);
    }
}

void Contents::append(cz::Str str) {
    ZoneScoped;

    if (buckets.len() == 0) {
        CZ_DEBUG_ASSERT(len == 0);
        insert_empty(this, str);
    } else {
        insert_at(this, end(), str);
    }
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
    ZoneScoped;
    string->reserve(allocator, len);
    slice_impl(string->buffer(), buckets, start(), len);
    string->set_len(string->len() + len);
}

cz::String Contents::stringify(cz::Allocator allocator) const {
    ZoneScoped;
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
    ZoneScoped;
    slice_impl(string, buckets, start, end - start.position);
}

void Contents::slice_into(Contents_Iterator start, uint64_t end, cz::String* string) const {
    ZoneScoped;
    CZ_DEBUG_ASSERT(string->cap() - string->len() >= end - start.position);
    slice_impl(string->end(), buckets, start, end - start.position);
    string->set_len(string->len() + end - start.position);
}

void Contents::slice_into(cz::Allocator allocator,
                          Contents_Iterator start,
                          uint64_t end,
                          cz::String* string) const {
    ZoneScoped;
    string->reserve(allocator, end - start.position);
    slice_into(start, end, string);
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

uint64_t Contents::get_line_number(uint64_t pos) const {
    ZoneScoped;

    uint64_t line = 0;
    for (size_t i = 0; i < buckets.len(); ++i) {
        if (pos < buckets[i].len) {
            return line + count_lines({buckets[i].elems, pos});
        }

        line += bucket_lfs[i];
        pos -= buckets[i].len;
    }

    // Handle eof.
    if (pos == 0) {
        return line;
    }

    CZ_PANIC("Out of bounds");
}

Contents_Iterator Contents::iterator_at(uint64_t pos) const {
    ZoneScoped;

    CZ_DEBUG_ASSERT(pos <= len);

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

    CZ_DEBUG_ASSERT(pos == 0);

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
    while (1) {
        if (bucket == contents->buckets.len()) {
            CZ_DEBUG_ASSERT(index == 0);
            break;
        }

        if (index < contents->buckets[bucket].len) {
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
