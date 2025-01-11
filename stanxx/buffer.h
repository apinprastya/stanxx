#pragma once

#include <cstring>
#include <span>
#include <string>
#include <vector>

class CharBuffer {
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

    const std::vector<char>& getBuffer () const {
        return buffer;
    }

    private:
    std::vector<char> buffer;
};