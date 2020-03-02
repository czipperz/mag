#pragma once

#include <mutex>
#include "buffer.hpp"

#ifdef NDEBUG
#include <cz/assert.hpp>
#endif

namespace mag {

class Buffer_Handle {
    std::mutex* mutex;
    Buffer* buffer;

#ifndef NDEBUG
    int simultaneous_access_count;
#endif

public:
    void init(Buffer_Id id, cz::Str name, cz::Option<cz::Str> directory) {
        mutex = new std::mutex();
        buffer = new Buffer();
        *buffer = {};
        buffer->init(id, name, directory);

#ifndef NDEBUG
        simultaneous_access_count = 0;
#endif
    }

    Buffer* lock() {
        mutex->lock();

#ifndef NDEBUG
        ++simultaneous_access_count;
        CZ_ASSERT(simultaneous_access_count == 1);
#endif

        return buffer;
    }

    void unlock() {
#ifndef NDEBUG
        --simultaneous_access_count;
#endif

        mutex->unlock();
    }

    void drop() {
        delete mutex;
        buffer->drop();
        delete buffer;
    }
};

}
