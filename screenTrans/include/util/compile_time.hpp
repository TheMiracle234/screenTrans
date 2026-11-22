#pragma once
#include <boost/pfr.hpp>
#include <cstddef>
#include <type_traits>
#include <utility>

template <typename T>
constexpr std::size_t count_basic_members();

// 展开索引序列，对每个成员递归
template <typename T, std::size_t... I>
constexpr std::size_t count_basic_members_impl(std::index_sequence<I...>) {
    return (std::size_t{ 0 } + ... + count_basic_members<
        std::remove_cv_t<std::remove_reference_t<
        decltype(boost::pfr::get<I>(std::declval<T&>()))
        >>
        >());
}

template <typename T>
constexpr std::size_t count_basic_members() {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
        constexpr std::size_t N = boost::pfr::tuple_size_v<U>;
        return count_basic_members_impl<U>(std::make_index_sequence<N>{});
    }
    else {
        return 1;   // 标量、指针、非聚合类（如 std::string）都算一个叶子
    }
}

template <typename T>
consteval std::size_t decl_align() {
    if constexpr (std::is_array_v<T>) {
        return decl_align<std::remove_extent_t<T>>();
    }
    else {
        constexpr std::size_t sz = sizeof(T);
        static_assert((sz & (sz - 1)) == 0, "sizeof must be power of 2");
        static_assert(alignof(T) <= sz, "natural align exceeds size");
        return sz;
    }
}

#define ALIGN(type) alignas(decl_align<type>()) type