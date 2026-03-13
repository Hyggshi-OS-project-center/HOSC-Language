/*
 * File: runtime\src\hosc_cpp_api.cpp
 * Purpose: HOSC source file.
 */

#include "hosc_cpp_api.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace {

static std::size_t align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0) return value;
    const std::size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

} // namespace

namespace hosc {

Arena::Arena(std::size_t capacity_bytes)
    : buffer_(capacity_bytes > 0 ? capacity_bytes : 1), offset_(0) {}

void* Arena::allocate(std::size_t size, std::size_t alignment) {
    if (size == 0) size = 1;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        alignment = alignof(std::max_align_t);
    }

    const std::size_t start = align_up(offset_, alignment);
    const std::size_t end = start + size;
    if (end < start || end > buffer_.size()) {
        return nullptr;
    }

    offset_ = end;
    return buffer_.data() + start;
}

void Arena::reset() {
    offset_ = 0;
}

std::size_t Arena::used() const {
    return offset_;
}

std::size_t Arena::capacity() const {
    return buffer_.size();
}

ApiContext::ApiContext(std::size_t arena_size) : arena_(arena_size) {}

void* ApiContext::alloc(std::size_t size, std::size_t alignment) {
    return arena_.allocate(size, alignment);
}

void ApiContext::set_int(const std::string& key, std::int64_t value) {
    int_store_[key] = value;
}

bool ApiContext::get_int(const std::string& key, std::int64_t& out) const {
    const auto it = int_store_.find(key);
    if (it == int_store_.end()) return false;
    out = it->second;
    return true;
}

void ApiContext::set_string(const std::string& key, const std::string& value) {
    string_store_[key] = value;
}

bool ApiContext::get_string(const std::string& key, std::string& out) const {
    const auto it = string_store_.find(key);
    if (it == string_store_.end()) return false;
    out = it->second;
    return true;
}

void ApiContext::clear() {
    arena_.reset();
    int_store_.clear();
    string_store_.clear();
}

std::size_t ApiContext::used_bytes() const {
    return arena_.used();
}

std::size_t ApiContext::capacity_bytes() const {
    return arena_.capacity();
}

} // namespace hosc

struct HoscApiContext {
    hosc::ApiContext impl;

    explicit HoscApiContext(std::size_t arena_size) : impl(arena_size) {}
};

extern "C" {

HoscApiContext* hosc_api_create(std::size_t arena_size) {
    try {
        return new HoscApiContext(arena_size);
    } catch (...) {
        return nullptr;
    }
}

void hosc_api_destroy(HoscApiContext* ctx) {
    delete ctx;
}

void* hosc_api_alloc(HoscApiContext* ctx, std::size_t size, std::size_t alignment) {
    if (!ctx) return nullptr;
    return ctx->impl.alloc(size, alignment);
}

int hosc_api_set_int(HoscApiContext* ctx, const char* key, std::int64_t value) {
    if (!ctx || !key) return 0;
    ctx->impl.set_int(key, value);
    return 1;
}

int hosc_api_get_int(HoscApiContext* ctx, const char* key, std::int64_t* out_value) {
    if (!ctx || !key || !out_value) return 0;

    std::int64_t value = 0;
    if (!ctx->impl.get_int(key, value)) return 0;
    *out_value = value;
    return 1;
}

int hosc_api_set_string(HoscApiContext* ctx, const char* key, const char* value) {
    if (!ctx || !key || !value) return 0;
    ctx->impl.set_string(key, value);
    return 1;
}

int hosc_api_get_string(HoscApiContext* ctx, const char* key, const char** out_value) {
    if (!ctx || !key || !out_value) return 0;

    std::string tmp;
    if (!ctx->impl.get_string(key, tmp)) return 0;

    const std::size_t size = tmp.size() + 1;
    char* mem = static_cast<char*>(ctx->impl.alloc(size, alignof(char)));
    if (!mem) return 0;

    std::memcpy(mem, tmp.c_str(), size);
    *out_value = mem;
    return 1;
}

void hosc_api_clear(HoscApiContext* ctx) {
    if (!ctx) return;
    ctx->impl.clear();
}

std::size_t hosc_api_used_bytes(const HoscApiContext* ctx) {
    if (!ctx) return 0;
    return ctx->impl.used_bytes();
}

std::size_t hosc_api_capacity_bytes(const HoscApiContext* ctx) {
    if (!ctx) return 0;
    return ctx->impl.capacity_bytes();
}

} // extern "C"

