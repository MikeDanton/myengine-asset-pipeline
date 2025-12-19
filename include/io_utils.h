// io_utils.h
#pragma once

#include <istream>
#include <ostream>
#include <cstring>

namespace asset::util {

template <typename T>
bool read_struct(std::istream &is, T &out) {
    is.read(reinterpret_cast<char*>(&out), sizeof(T));
    return !!is;
}

template <typename T>
bool write_struct(std::ostream &os, const T &in) {
    os.write(reinterpret_cast<const char*>(&in), sizeof(T));
    return !!os;
}

} // namespace asset::util
