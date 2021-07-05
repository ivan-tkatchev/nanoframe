#pragma once

#include <iostream>
#include <fstream>

#include <cmath>
#include <limits>
#include <compare>
#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <tuple>
#include <unordered_map>
#include <functional>

namespace nanoframe {

// Dataframe column.
    
template <typename T>
using Column = std::vector<T>;

// String interning for factors.

template <typename TAG, typename Int>
std::unordered_map<std::string, Int>& intern_table() {
    static std::unordered_map<std::string, Int> ret;
    return ret;
}

struct null_symbol {};

template <typename TAG, typename Int = std::uint16_t>
struct Symbol {
    using type = Int;
    using tag = TAG;
    Int val;

    Symbol(null_symbol) : val(0) {}

    Symbol(const std::string& s) {
        auto& table = intern_table<TAG, Int>();
        auto i = table.find(s);

        if (i == table.end()) {
            table[s] = table.size() + 1;
            i = table.find(s);
        }

        val = i->second;
    }

    template <size_t N>
    Symbol(const char (&str)[N]) : Symbol(std::string(str)) {}

    template <typename JSON>
    requires requires(JSON s) { s.template get<std::string>(); }
    Symbol(const JSON& s) : Symbol(s.template get<std::string>()) {}

    bool null() const {
        return val == 0;
    }

    bool ok() const {
        return val != 0;
    }

    auto operator<=>(const Symbol<TAG, Int>& a) const = default;
};

}

namespace std {
    template <typename T>
    struct underlying_type<nanoframe::Symbol<T>> {
        using type = typename nanoframe::Symbol<T>::type;
    };
}

namespace nanoframe {

// Grouping and summarizing indexes.

template <typename Int = std::uint32_t>
struct Index {
    Column<Int> ix;
    Column<Int> group_start;

    struct range_t {
        Int group_head;
        Column<Int>::const_iterator b;
        Column<Int>::const_iterator e;

        template <typename FUNC>
        void for_each(FUNC f) const {

            for (auto i = b; i != e; ++i) {
                f(*i);
            }
        }

        size_t size() const {
            return e - b;
        }
    };

    template <typename FUNC>
    Index(size_t n, FUNC vals) : ix(n), group_start(n) {
        for (Int i = 0; i < ix.size(); ++i) {
            ix[i] = i;
        }

        std::sort(ix.begin(), ix.end(),
                  [&](Int a, Int b) {
                      return vals(a) < vals(b);
                  });

        if (ix.empty()) {
            return;
        }

        group_start[0] = ix[0];
        Int prev_i = group_start[0];

        for (Int i = 1; i < ix.size(); ++i) {
            if (vals(ix[prev_i]) != vals(ix[i])) {
                prev_i = i;
            }
            group_start[i] = ix[prev_i];
        }        
    }

    template <typename FUNC>
    Index<Int> merge(FUNC f) {
        size_t prev_i = 0;
        for (size_t i = 0; i < ix.size(); ++i) {

            if (group_start[i] != group_start[prev_i]) {
                f(range_t{group_start[prev_i], ix.begin() + prev_i, ix.begin() + i});
                prev_i = i;
            }
        }

        if (group_start.size() > 0) {
            f(range_t{group_start[prev_i], ix.begin() + prev_i, ix.end()});
        }

        return *this;
    }

    Column<Int> indexes() const {
        Column<Int> ret = group_start;

        ret.erase(std::unique(ret.begin(), ret.end()), ret.end());

        return ret;
    }
};

// Utility functions.

namespace {

template <typename T>
concept Container = requires (T t) { t.size(); };

}

auto transform(Container auto& t, const Container auto& s, auto f) -> decltype(t) {
    if (t.size() != s.size()) {
        throw std::runtime_error("Column size mismatch in nanoframe::transform: " + std::to_string(t.size()) + " != " + std::to_string(s.size()));
    }

    for (size_t i = 0; i < t.size(); ++i) {
        t[i] = f(t[i], s[i]);
    }

    return t;
}

template <Container T>
auto transform(const T& t, const Container auto& s, auto f) -> T {
    if (t.size() != s.size()) {
        throw std::runtime_error("Column size mismatch in nanoframe::transform: " + std::to_string(t.size()) + " != " + std::to_string(s.size()));
    }

    T ret(t);

    for (size_t i = 0; i < t.size(); ++i) {
        ret[i] = f(t[i], s[i]);
    }

    return ret;
}

auto transform(Container auto& t, const auto& v, auto f) -> decltype(t) {
    for (auto& val : t) {
        val = f(val, v);
    }

    return t;
}

template <Container T>
auto transform(const T& t, const auto& v, auto f) -> T {
    T ret(t);

    for (auto& val : ret) {
        val = f(val, v);
    }

    return ret;
}

template <typename T, typename Int>
Column<T> filter(const Column<T>& t, const Column<Int>& ix) {
    Column<T> ret;

    for (auto i : ix) {
        ret.push_back(t[i]);
    }

    return ret;
}

// Dataframe columns tuple

namespace {

template <typename T>
concept Tuple = requires (T t) { std::tuple_cat(std::make_tuple(1), t); };

}

template <auto ... MEMS>
struct Columns {

    static void apply(auto&& frame, auto func) {
        (func(frame.*MEMS), ...);
    }

    static void for_each(auto&& frame, auto ... func) {
        (func(frame.*MEMS), ...);
    }

    static void for_each(auto&& frame, auto func, Tuple auto&& ... params) {
        (std::apply(func, std::tuple_cat(std::forward_as_tuple(frame.*MEMS), params)), ...);
    }

    static void combine(auto&& frame1, auto&& frame2, auto func) {
        (func(frame1.*MEMS, frame2.*MEMS), ...);
    }
};

namespace tuple {

void apply(auto&& tuple, auto func) {
    std::apply([&](auto&& ... mem) {
        (func(mem), ...);
    }, tuple);
}

void for_each(auto&& tuple, auto ... func) {
    std::apply([&](auto&& ... mem) {
        (func(mem), ...);
    }, tuple);
}

void for_each(auto&& tuple, auto func, Tuple auto&& ... params) {
    std::apply([&](auto&& ... mem) {
        (std::apply(func, std::tuple_cat(std::forward_as_tuple(mem), params)), ...);
    }, tuple);
}

void combine(auto&& tuple1, auto&& tuple2, auto func) {
    std::apply([&](auto&& ... a) {
        std::apply([&](auto&& ... b) {
            (func(a, b), ...);
        }, tuple2);
    }, tuple1);
}

}

}
