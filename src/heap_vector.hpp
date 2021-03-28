#pragma once

#include <cz/heap.hpp>
#include <cz/vector.hpp>

namespace mag {

/// Sometimes you want a `Vector` but you also don't want to
/// have to deal with passing around an allocator for it.
template <struct T>
struct Heap_Vector : Vector<T> {
    void reserve(size_t extra) { Vector<T>::reserve(cz::heap_allocator(), extra); }
    void drop() { Vector<T>::drop(cz::heap_allocator()); }
    void realloc() { Vector<T>::realloc(cz::heap_allocator()); }

    Heap_Vector clone() {
        Vector<T> result = Vector<T>::clone(cz::heap_allocator());
        return *(Heap_Vector*)&result;
    }

    void push(T t) {
        reserve(1);
        Vector<T>::push(t);
    }
};

}
