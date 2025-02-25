#pragma once

#include <cstring>
#include <span>
#include <string>
#include <vector>

/*class CharBuffer {
    public:
    template <typename T> void write (const T& value) {
        const char* data = reinterpret_cast<const char*> (&value);
        buffer.insert (buffer.end (), data, data + sizeof (T));
    }

    void write (const void* data, size_t size) {
        const char* bytes = static_cast<const char*> (data);
        buffer.insert (buffer.end (), bytes, bytes + size);
    }

    void write (const char* str) {
        size_t len = std::strlen (str);
        write (str, len);
    }

    void write (const std::string& str) {
        buffer.insert (buffer.end (), str.begin (), str.end ());
    }

    void write (const std::span<char>& data) {
        buffer.insert (buffer.end (), data.begin (), data.end ());
    }

    std::vector<char>& getBuffer () {
        return buffer;
    }

    private:
    std::vector<char> buffer;
};*/

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