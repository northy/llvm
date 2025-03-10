//==-- property_utils.hpp --- SYCL extended property list common utilities -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#pragma once

#include <sycl/detail/property_helper.hpp>
#include <sycl/ext/oneapi/properties/property.hpp>

#include <tuple>

namespace sycl {
__SYCL_INLINE_VER_NAMESPACE(_V1) {
namespace ext {
namespace oneapi {
namespace experimental {

// Forward declaration
template <typename PropertyT, typename... Ts> struct property_value;

namespace detail {

//******************************************************************************
// Misc
//******************************************************************************

// Checks if a type is a tuple.
template <typename T> struct IsTuple : std::false_type {};
template <typename... Ts> struct IsTuple<std::tuple<Ts...>> : std::true_type {};

// Gets the first type in a parameter pack of types.
template <typename... Ts>
using GetFirstType = typename std::tuple_element<0, std::tuple<Ts...>>::type;

// Prepends a value to a tuple.
template <typename T, typename Tuple> struct PrependTuple {};
template <typename T, typename... Ts>
struct PrependTuple<T, std::tuple<Ts...>> {
  using type = std::tuple<T, Ts...>;
};

// Checks if a type T has a static value member variable.
template <typename T, typename U = int> struct HasValue : std::false_type {};
template <typename T>
struct HasValue<T, decltype((void)T::value, 0)> : std::true_type {};

//******************************************************************************
// Property identification
//******************************************************************************

// Checks if a type is a compile-time property values.
// Note: This is specialized for property_value elsewhere.
template <typename PropertyT>
struct IsCompileTimePropertyValue : std::false_type {};

// Checks if a type is either a runtime property or if it is a compile-time
// property
template <typename T> struct IsProperty {
  static constexpr bool value =
      IsRuntimeProperty<T>::value || IsCompileTimeProperty<T>::value;
};

// Checks if a type is a valid property value, i.e either runtime property or
// property_value with a valid compile-time property
template <typename T> struct IsPropertyValue {
  static constexpr bool value =
      IsRuntimeProperty<T>::value || IsCompileTimePropertyValue<T>::value;
};

// Checks that all types in a tuple are valid properties.
template <typename T> struct AllPropertyValues {};
template <typename... Ts>
struct AllPropertyValues<std::tuple<Ts...>> : std::true_type {};
template <typename T, typename... Ts>
struct AllPropertyValues<std::tuple<T, Ts...>>
    : std::conditional_t<IsPropertyValue<T>::value,
                         AllPropertyValues<std::tuple<Ts...>>,
                         std::false_type> {};

//******************************************************************************
// Property type sorting
//******************************************************************************

// Splits a tuple into head and tail if ShouldSplit is true. If ShouldSplit is
// false the head will be void and the tail will be the full tuple.
template <typename T1, bool ShouldSplit> struct HeadSplit {};
template <typename T, typename... Ts>
struct HeadSplit<std::tuple<T, Ts...>, true> {
  using htype = T;
  using ttype = std::tuple<Ts...>;
};
template <typename... Ts> struct HeadSplit<std::tuple<Ts...>, false> {
  using htype = void;
  using ttype = std::tuple<Ts...>;
};

// Selects the one of two types that is not void. This assumes that at least one
// of the two template arguemnts is void.
template <typename LHS, typename RHS> struct SelectNonVoid {};
template <typename LHS> struct SelectNonVoid<LHS, void> {
  using type = LHS;
};
template <typename RHS> struct SelectNonVoid<void, RHS> {
  using type = RHS;
};

// Merges two tuples by recursively extracting the type with the minimum
// PropertyID in the two tuples and prepending it to the merging of the
// remaining elements.
template <typename T1, typename T2> struct Merge {};
template <typename... LTs> struct Merge<std::tuple<LTs...>, std::tuple<>> {
  using type = std::tuple<LTs...>;
};
template <typename... RTs> struct Merge<std::tuple<>, std::tuple<RTs...>> {
  using type = std::tuple<RTs...>;
};
template <typename... LTs, typename... RTs>
struct Merge<std::tuple<LTs...>, std::tuple<RTs...>> {
  using l_head = GetFirstType<LTs...>;
  using r_head = GetFirstType<RTs...>;
  static constexpr bool left_has_min =
      PropertyID<l_head>::value < PropertyID<r_head>::value;
  using l_split = HeadSplit<std::tuple<LTs...>, left_has_min>;
  using r_split = HeadSplit<std::tuple<RTs...>, !left_has_min>;
  using min = typename SelectNonVoid<typename l_split::htype,
                                     typename r_split::htype>::type;
  using merge_tails =
      typename Merge<typename l_split::ttype, typename r_split::ttype>::type;
  using type = typename PrependTuple<min, merge_tails>::type;
};

// Creates pairs of tuples with a single element from a tuple with N elements.
// Resulting tuple will have ceil(N/2) elements.
template <typename...> struct CreateTuplePairs {
  using type = typename std::tuple<>;
};
template <typename T> struct CreateTuplePairs<T> {
  using type = typename std::tuple<std::pair<std::tuple<T>, std::tuple<>>>;
};
template <typename L, typename R, typename... Rest>
struct CreateTuplePairs<L, R, Rest...> {
  using type =
      typename PrependTuple<std::pair<std::tuple<L>, std::tuple<R>>,
                            typename CreateTuplePairs<Rest...>::type>::type;
};

// Merges pairs of tuples and creates new pairs of the merged pairs. Let N be
// the number of pairs in the supplied tuple, then the resulting tuple will
// contain ceil(N/2) pairs of tuples.
template <typename T> struct MergePairs {
  using type = std::tuple<>;
};
template <typename... LTs, typename... RTs, typename... Rest>
struct MergePairs<
    std::tuple<std::pair<std::tuple<LTs...>, std::tuple<RTs...>>, Rest...>> {
  using merged = typename Merge<std::tuple<LTs...>, std::tuple<RTs...>>::type;
  using type = std::tuple<std::pair<merged, std::tuple<>>>;
};
template <typename... LLTs, typename... LRTs, typename... RLTs,
          typename... RRTs, typename... Rest>
struct MergePairs<
    std::tuple<std::pair<std::tuple<LLTs...>, std::tuple<LRTs...>>,
               std::pair<std::tuple<RLTs...>, std::tuple<RRTs...>>, Rest...>> {
  using lmerged =
      typename Merge<std::tuple<LLTs...>, std::tuple<LRTs...>>::type;
  using rmerged =
      typename Merge<std::tuple<RLTs...>, std::tuple<RRTs...>>::type;
  using type = typename PrependTuple<
      std::pair<lmerged, rmerged>,
      typename MergePairs<std::tuple<Rest...>>::type>::type;
};

// Recursively merges all pairs of tuples until only a single pair of tuples
// is left, where the right element of the pair is an empty tuple.
template <typename T> struct MergeAll {};
template <typename... Ts> struct MergeAll<std::tuple<Ts...>> {
  using type = std::tuple<Ts...>;
};
template <typename... Ts>
struct MergeAll<std::tuple<std::pair<std::tuple<Ts...>, std::tuple<>>>> {
  using type = std::tuple<Ts...>;
};
template <typename T, typename... Ts> struct MergeAll<std::tuple<T, Ts...>> {
  using reduced = typename MergePairs<std::tuple<T, Ts...>>::type;
  using type = typename MergeAll<reduced>::type;
};

// Performs merge-sort on types with PropertyID.
template <typename... Ts> struct Sorted {
  static_assert(detail::AllPropertyValues<std::tuple<Ts...>>::value,
                "Unrecognized property in property list.");
  using split = typename CreateTuplePairs<Ts...>::type;
  using type = typename MergeAll<split>::type;
};

// Checks if the types in a tuple are sorted w.r.t. their PropertyID.
template <typename T> struct IsSorted {};
template <typename... Ts>
struct IsSorted<std::tuple<Ts...>> : std::true_type {};
template <typename T> struct IsSorted<std::tuple<T>> : std::true_type {};
template <typename L, typename R, typename... Rest>
struct IsSorted<std::tuple<L, R, Rest...>>
    : std::conditional_t<PropertyID<L>::value <= PropertyID<R>::value,
                         IsSorted<std::tuple<R, Rest...>>, std::false_type> {};

// Checks that all types in a sorted tuple have unique PropertyID.
template <typename T> struct SortedAllUnique {};
template <typename... Ts>
struct SortedAllUnique<std::tuple<Ts...>> : std::true_type {};
template <typename T> struct SortedAllUnique<std::tuple<T>> : std::true_type {};
template <typename L, typename R, typename... Rest>
struct SortedAllUnique<std::tuple<L, R, Rest...>>
    : std::conditional_t<PropertyID<L>::value != PropertyID<R>::value,
                         SortedAllUnique<std::tuple<R, Rest...>>,
                         std::false_type> {};

//******************************************************************************
// Property merging
//******************************************************************************

// Merges two sets of properties, failing if two properties are the same but
// with different values.
// NOTE: This assumes that the properties are in sorted order.
template <typename LHSPropertyT, typename RHSPropertyT> struct MergeProperties;

template <> struct MergeProperties<std::tuple<>, std::tuple<>> {
  using type = std::tuple<>;
};

template <typename... LHSPropertyTs>
struct MergeProperties<std::tuple<LHSPropertyTs...>, std::tuple<>> {
  using type = std::tuple<LHSPropertyTs...>;
};

template <typename... RHSPropertyTs>
struct MergeProperties<std::tuple<>, std::tuple<RHSPropertyTs...>> {
  using type = std::tuple<RHSPropertyTs...>;
};

// Identical properties are allowed, but only one will carry over.
template <typename PropertyT, typename... LHSPropertyTs,
          typename... RHSPropertyTs>
struct MergeProperties<std::tuple<PropertyT, LHSPropertyTs...>,
                       std::tuple<PropertyT, RHSPropertyTs...>> {
  using merge_tails =
      typename MergeProperties<std::tuple<LHSPropertyTs...>,
                               std::tuple<RHSPropertyTs...>>::type;
  using type = typename PrependTuple<PropertyT, merge_tails>::type;
};

template <typename... LHSPropertyTs, typename... RHSPropertyTs>
struct MergeProperties<std::tuple<LHSPropertyTs...>,
                       std::tuple<RHSPropertyTs...>> {
  using l_head = GetFirstType<LHSPropertyTs...>;
  using r_head = GetFirstType<RHSPropertyTs...>;
  static_assert(
      PropertyID<l_head>::value != PropertyID<r_head>::value,
      "Failed to merge property lists due to conflicting properties.");
  static constexpr bool left_has_min =
      PropertyID<l_head>::value < PropertyID<r_head>::value;
  using l_split = HeadSplit<std::tuple<LHSPropertyTs...>, left_has_min>;
  using r_split = HeadSplit<std::tuple<RHSPropertyTs...>, !left_has_min>;
  using min = typename SelectNonVoid<typename l_split::htype,
                                     typename r_split::htype>::type;
  using merge_tails = typename MergeProperties<typename l_split::ttype,
                                               typename r_split::ttype>::type;
  using type = typename PrependTuple<min, merge_tails>::type;
};

//******************************************************************************
// Property value tooling
//******************************************************************************

// Simple helpers for containing primitive types as template arguments.
template <size_t... Sizes> struct SizeList {};
template <char... Sizes> struct CharList {};

// Helper for converting characters to a constexpr string.
template <char... Chars> struct CharsToStr {
  static inline constexpr const char value[] = {Chars..., '\0'};
};

// Helper for converting a list of size_t values to a comma-separated string
// representation. This is done by extracting the digit one-by-one and when
// finishing a value, the parsed result is added to a separate list of
// "parsed" characters with the delimiter.
template <typename List, typename ParsedList, char... Chars>
struct SizeListToStrHelper;

// Specialization for when we are in the process of converting a non-zero value
// (Value). Chars will have the already converted digits of the original value
// being converted. Instantiation of this will convert the least significant
// digit in Value.
// Example:
//  - Current: SizeListToStrHelper<SizeList<12>, CharList<'1', '0', ','>, '3'>
//  - Next: SizeListToStrHelper<SizeList<1>, CharList<'1', '0', ','>, '2', '3'>
//  - Outermost: SizeListToStrHelper<SizeList<10,123>, CharList<>>
//  - Final: SizeListToStrHelper<SizeList<0>,
//                               CharList<'1', '0', ','>, '1', '2', '3'>>
//  - Result string: "10,123"
template <size_t Value, size_t... Values, char... ParsedChars, char... Chars>
struct SizeListToStrHelper<SizeList<Value, Values...>, CharList<ParsedChars...>,
                           Chars...>
    : SizeListToStrHelper<SizeList<Value / 10, Values...>,
                          CharList<ParsedChars...>, '0' + (Value % 10),
                          Chars...> {};

// Specialization for when we have reached 0 in the current value we are
// converting. In this case we are done with converting the current value and
// we insert the converted digits from Chars into ParsedChars.
// Example:
//  - Current: SizeListToStrHelper<SizeList<0,123>, CharList<>, '1', '0'>
//  - Next: SizeListToStrHelper<SizeList<123>, CharList<'1', '0', ','>>
//  - Outermost: SizeListToStrHelper<SizeList<10,123>, CharList<>>
//  - Final: SizeListToStrHelper<SizeList<0>,
//                               CharList<'1', '0', ','>, '1', '2', '3'>>
//  - Result string: "10,123"
template <size_t... Values, char... ParsedChars, char... Chars>
struct SizeListToStrHelper<SizeList<0, Values...>, CharList<ParsedChars...>,
                           Chars...>
    : SizeListToStrHelper<SizeList<Values...>,
                          CharList<ParsedChars..., Chars..., ','>> {};

// Specialization for the special case where the value we are converting is 0
// but the list of converted digits is empty. This means there was a 0 value in
// the list and we can add it to ParsedChars directly.
// Example:
//  - Current: SizeListToStrHelper<SizeList<0,123>, CharList<>>
//  - Next: SizeListToStrHelper<SizeList<123>, CharList<'0', ','>>
//  - Outermost: SizeListToStrHelper<SizeList<0,123>, CharList<>>
//  - Final: SizeListToStrHelper<SizeList<0>,
//                               CharList<'0', ','>, '1', '2', '3'>>
//  - Result string: "0,123"
template <size_t... Values, char... ParsedChars>
struct SizeListToStrHelper<SizeList<0, Values...>, CharList<ParsedChars...>>
    : SizeListToStrHelper<SizeList<Values...>,
                          CharList<ParsedChars..., '0', ','>> {};

// Specialization for when we have reached 0 in the current value we are
// converting and there a no more values to parse. In this case we are done with
// converting the current value and we insert the converted digits from Chars
// into ParsedChars. We do not add a ',' as it is the end of the list.
// Example:
//  - Current: SizeListToStrHelper<SizeList<0>, CharList<'1', '0', ','>, '1',
//  '2', '3'>>
//  - Next: None.
//  - Outermost: SizeListToStrHelper<SizeList<10,123>, CharList<>>
//  - Final: SizeListToStrHelper<SizeList<0>,
//                               CharList<'1', '0', ','>, '1', '2', '3'>>
//  - Result string: "10,123"
template <char... ParsedChars, char... Chars>
struct SizeListToStrHelper<SizeList<0>, CharList<ParsedChars...>, Chars...>
    : CharsToStr<ParsedChars..., Chars...> {};

// Specialization for when we have reached 0 in the current value we are
// converting and there a no more values to parse, but the list of converted
// digits is empty. This means the last value in the list was a 0 so we can add
// that to the ParsedChars and finish.
// Example:
//  - Current: SizeListToStrHelper<SizeList<0>, CharList<'1', '0', ','>>>
//  - Next: None.
//  - Outermost: SizeListToStrHelper<SizeList<10,0>, CharList<>>
//  - Final: SizeListToStrHelper<SizeList<0>, CharList<>, '1', '0'>>
//  - Result string: "10,0"
template <char... ParsedChars>
struct SizeListToStrHelper<SizeList<0>, CharList<ParsedChars...>>
    : CharsToStr<ParsedChars..., '0'> {};

// Specialization for the empty list of values to convert. This results in an
// empty string.
template <>
struct SizeListToStrHelper<SizeList<>, CharList<>> : CharsToStr<> {};

// Converts size_t values to a comma-separated string representation.
template <size_t... Sizes>
struct SizeListToStr : SizeListToStrHelper<SizeList<Sizes...>, CharList<>> {};

} // namespace detail
} // namespace experimental
} // namespace oneapi
} // namespace ext
} // __SYCL_INLINE_VER_NAMESPACE(_V1)
} // namespace sycl
