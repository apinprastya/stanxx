#pragma once

#include <cstring>
#include <vector>

class CharBuffer {
    std::vector<char> _buffer;

    public:
    void reserve (size_t size) {
        _buffer.reserve (size);
    }

    void clear () {
        _buffer.clear (); // Keeps capacity
    }

    void write (const char* data, size_t len) {
        size_t oldSize = _buffer.size ();
        _buffer.resize (oldSize + len);
        std::memcpy (_buffer.data () + oldSize, data, len);
    }

    std::vector<char>&& getBuffer () && {
        return std::move (_buffer);
    }
};