// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cortexflow {

using type_id_t = std::uint64_t;

namespace detail {

template <typename T>
constexpr std::string_view type_name_raw() {
#if defined(__clang__) || defined(__GNUC__)
    return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    return __FUNCSIG__;
#else
    static_assert(sizeof(T) == 0, "Unsupported compiler for type_name");
#endif
}

constexpr std::string_view type_name_probe = type_name_raw<void>();
constexpr std::size_t type_name_prefix = type_name_probe.find("void");
constexpr std::size_t type_name_suffix =
    type_name_probe.size() - type_name_prefix - 4;

constexpr type_id_t fnv1a(std::string_view s) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (char c : s) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace detail

template <typename T>
constexpr std::string_view type_name() {
    constexpr std::string_view raw = detail::type_name_raw<T>();
    return raw.substr(detail::type_name_prefix,
                      raw.size() - detail::type_name_prefix -
                          detail::type_name_suffix);
}

template <typename T>
constexpr type_id_t type_id() {
    return detail::fnv1a(type_name<T>());
}

namespace detail {

// NUL-terminated copy of `type_name<T>()` held in a per-type static buffer.
// `type_name<T>()` returns a `std::string_view` carved out of
// `__PRETTY_FUNCTION__` — it is NOT NUL-terminated, so trace sites that
// hand strings to a `const char*`-taking sink need a terminated form.
template <typename T>
struct type_name_buffer {
    static constexpr auto kView = type_name<T>();
    static constexpr auto make() {
        std::array<char, kView.size() + 1> arr{};
        for (std::size_t i = 0; i < kView.size(); ++i) {
            arr[i] = kView[i];
        }
        arr[kView.size()] = '\0';
        return arr;
    }
    static constexpr auto kStorage = make();
};

} // namespace detail

// Per-type NUL-terminated `const char*` form of `type_name<T>()`.
template <typename T>
constexpr const char* type_name_cstr() {
    return detail::type_name_buffer<T>::kStorage.data();
}

template <typename Derived>
struct Identified {
    static constexpr std::string_view kName = type_name<Derived>();
    static constexpr type_id_t kTypeId = detail::fnv1a(kName);
};

} // namespace cortexflow
