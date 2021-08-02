# nanoframe

This is a minimal and idiomatic dataframe library for C++.

(C++20 is required; compile with `-std=c++-20`.)

Rather than re-implementing Pandas or dplyr workflows, an attempt is made to keep with native C++ structures and design patterns. 

This is a header-only library with no external dependencies. 

## API

### Datatypes 

```cpp
template <typename T>
using Column = ...;
```

This is a column of values of one type; currently just an alias for `std::vector`.

```cpp
struct null_symbol {};

template <typename TAG, typename Int = std::uint16_t>
struct Symbol {

  Symbol(null_symbol);
  
  Symbol(const std::string&);
  
  template <size_t N>
  Symbol(const char (&)[N])
  
  template <typename Any>
  requires requires(Any s) { s.template get<std::string>(); }
  Symbol(const Any& s);
  
  bool null() const;
  bool ok() const;
  auto operator<=>(const Symbol<TAG,INT>&);
};
```

This type maps strings to interned symbol identifiers. Support for 'null value symbols' is provided. 
`null()` and `ok()` both check for null.

Symbols map to 16-bit integers by default.

Besides strings, any type that supports a `get<std::string>()` method can be converted to a Symbol.

### Grouping and summarizing

```cpp
template <typename Int = std::uint32_t>
struct Index {
  
  struct range_t;
  
  Index(size_t, auto);
  
  Index<Int>& merge(auto);
  
  Column<Int> indexes() const;
};

```

Creates a sorted index and allows for summarizing over it.

`Index(size_t n, auto func)` -- initializes an index of size `n`, where the order of elements 0...n-1 is determined by calling `func(size_t i)`. 
The sorted index is then divided into groups by elements that are equal by calling `func(size_t i)`.

(Whatever is returned from `func()` should support `operator<` and `operator!=`.)

Note! The index never stores whatever is returned from `func()`; only a vector of `n` `Int`s needs to be stored in memory.

`indexes()` -- returns only those elements that are first in each group.

`merge(auto func)` -- for each group of equal elements in the index, calls `func(Index::range_t range)`. Returns `*this`.

The given range supports these operations:

```cpp
struct range_t {
   Int group_head;
   void for_each(auto);
   size_t size() const;
};
```

`for_each(auto func)` -- for each element in the range, call `func(Int i)` with the element.

`size()` -- returns the length of the range.

`group_head` -- is the first element in the range.

### Column-wise operations

```cpp
template <auto ... MEMS>
struct Columns {
  static void apply(auto&& frame, auto func);
  static void for_each(auto&& frame, auto ... func);
  static void for_each(auto&& frame, auto func, Tuple auto&& ... params);
  static void combine(auto&& frame1, auto&& frame2, auto func);
};
```

`Columns` is a type that is not meant to be instantiated. Its only purpose is to hold pointers-to-member of several data columns.
The following operations are supported:

`apply(auto&& frame, auto func)` -- for each pointer-to-member `MEMS`, calls `func(frame.*MEMS)`

`for_each(auto&& frame, auto ... func)` -- for each pointer-to-member `MEMS`, calls in turn each of `func`: `func(frame.*MEMS)`

`for_each(auto&& frame, auto func, Tuple auto&& ... params)` -- for each pointer-to-member `MEMS`, calls `func(frame.*MEMS, params...)` passing in arguments from each of the tuples `params`.

`combine(auto&& frame1, auto&& frame2, auto func)` -- combines `frame1` and `frame2` by calling `func(frame1.*MEMS, frame2.*MEMS)` for each pointer-to-member `MEMS`. 

```cpp
namespace tuple {
  void apply(auto&& tuple, auto func);
  void for_each(auto&& tuple, auto ... func);
  void for_each(auto&& tuple, auto func, Tuple auto&& ... params);
  void combine(auto&& tuple1, auto&& tuple2, auto func);
}
```

Functionally equivalent to the above `Columns`, except on tuples of columns instead of pointers-to-member.

### Utility functions


`auto transform(Container auto& t, const Container auto& s, auto f) -> decltype(t)` -- calls `t[i] = f(t[i], s[i])` on each element and returns `t`.
(`s` and `t` must be the same length.)

`template <Container T> auto transform(const T& t, const Container auto& s, auto f) -> T` -- same as above, except a copy of `t` is first made.

`auto transform(Container auto& t, const auto& v, auto f) -> decltype(t)` -- calls `t[i] = f(t[i], v)` on each element and returns `t`.

`template <Container T> auto transform(const T& t, const auto& v, auto f) -> T` -- same as above, except a copy of `t` is first made.

```
template <typename T, typename Int> 
Column<T> filter(const Column<T>& t, const Column<Int>& ix)
```

Makes a copy of `t` but leave only those whose index is found in `ix`.

## Examples

A non-functional but complete example:

```cpp
using namespace nanoframe;

namespace tags {
  struct ta;
  struct type;
}

using TA = Symbol<tags::ta>;
using Type = Symbol<tags::type>;

struct Frame {
    Column<TA> ta;
    Column<Type> type;
    Column<std::uint8_t> window;
    Column<double> tvr;
};

using FrameCols = Columns<
    &Frame::ta,
    &Frame::type,
    &Frame::window,
    &Frame::tvr>;

std::vector<Frame> frames;

/* ... Populate the contents of `frames` somehow ... */

Frame combined;

for (const Frame& frame : frames) {
  FrameCols::combine(combined, frame,
                     [](auto& a, const auto& b) { a.insert(a.end(), b.begin(), b.end()); });
}

size_t N = combined.ta.size();

Column<double> error(N);

Index(N,
      [&combined](auto i) {
          return std::make_tuple(combined.ta[i], combined.type[i]);
      }).merge([&combined, &error](auto range) {
      
         std::map<std::uint8_t, double> tvrs;
         
         range.for_each([&](size_t i) {
           tvrs[combined.window[i]] += combined.tvr[i];
         });
         
         range.for_each([&](size_t i) {
           double tvr = tvrs[combined.window[i]];
           error[i] = tvr - tvrs[0];
           combined.tvr[i] = tvr;
         });
      });

auto indexes = Index(N,
                     [&combined](auto i) {
                       return std::make_tuple(combined.ta[i], combined.type[i], combined.window[i]);
                     }).indexes();

auto output = [&](auto& column, const std::string& name) {
  do_output_somehow(filter(column, indexes), name);
};

FrameCols::for_each(combined, output,
                    std::tuple{"ta"},
                    std::tuple("type"},
                    std::tuple{"tvr"});
                    
output(error, "error");
```

Filtering dataframes:

```cpp
Frame new_combined;

Index(N,
      [&combined](auto i) {
          return /* grouping condition goes here */;
      }).merge([&combined, &new_combined](auto range) {
          range.for_each([&](size_t ix) {

              if (/* filter condition goes here */) {
                  FrameCols::combine(new_combined, combined,
                                     [&](auto& to_col, const auto& from_col) {
                                         to_col.push_back(from_col[ix]);
                                     });
              }
          });
      });
```

Or, if you don't need grouping:

```cpp
Frame new_combined;

for (size_t ix = 0; ix < N; ++ix) {
    if (/* filter condition goes here */) {
        FrameCols::combine(new_combined, combined,
                           [&](auto& to_col, const auto& from_col) {
                               to_col.push_back(from_col[ix]);
                           });
    }
}
```

An example of higher-ordered dataframes:

```cpp
template <typename ... KEY>
struct FrameGroup {
  using key_t = std::tuple<Column<TA>, KEY...>;
  key_t key;
  Column<double> median;

  FrameGroup(const Frame& combined, auto group_func) {
    Index(combined.ta.size(), group_func)
      .merge([&](auto range) {
      
        size_t N = range.size();
        
        if (N == 0) {
          return;
        }
      
        Column<double> tvr;
        tvr.reserve(N);
        
        range.for_each([&](auto i) {
          tvr.push_back(combined.tvr[i]);
        });
        
        std::sort(tvr.begin(), tvr.end());
        
        tuple::combine(group, group_func(range.group_head),
                       [&](auto&& column, auto&& value) {
                         column.push_back(value);
                       });
        median.push_back(tvr[N/2]);
      });
  }
};

FrameGroup<Column<std::uint8_t>> 
  by_window(combined, 
            [&](auto i) { return std::make_tuple(combined.window[i]); });

FrameGroup<Column<Type>> 
  by_type(combined,
          [&](auto i) { return std::make_tuple(combined.type[i]); });

FrameGroup<Column<std::uint8_t>, Column<Type>>
by_window_type(combined,
               [&](auto i) { return std::make_tuple(combined.window[i], combined.type[i]); });

/* Do something with by_window.median, by_type.median and by_window_type.median */

```
