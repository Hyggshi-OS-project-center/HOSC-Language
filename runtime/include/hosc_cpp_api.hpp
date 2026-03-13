/*
 * File: runtime\include\hosc_cpp_api.hpp
 * Purpose: HOSC source file.
 */

#ifndef HOSC_CPP_API_HPP
#define HOSC_CPP_API_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "hosc_cpp_api.h"

namespace hosc {

class Arena {
public:
    explicit Arena(std::size_t capacity_bytes = 1024 * 1024);

    void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

    template <typename T, typename... Args>
    T* create(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        return new (mem) T(static_cast<Args&&>(args)...);
    }

    void reset();
    std::size_t used() const;
    std::size_t capacity() const;

private:
    std::vector<unsigned char> buffer_;
    std::size_t offset_;
};

class ApiContext {
public:
    explicit ApiContext(std::size_t arena_size = 1024 * 1024);

    void* alloc(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

    void set_int(const std::string& key, std::int64_t value);
    bool get_int(const std::string& key, std::int64_t& out) const;

    void set_string(const std::string& key, const std::string& value);
    bool get_string(const std::string& key, std::string& out) const;

    void clear();

    std::size_t used_bytes() const;
    std::size_t capacity_bytes() const;

private:
    Arena arena_;
    std::unordered_map<std::string, std::int64_t> int_store_;
    std::unordered_map<std::string, std::string> string_store_;
};

} // namespace hosc

#endif // HOSC_CPP_API_HPP
