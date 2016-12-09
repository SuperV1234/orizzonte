// Copyright (c) 2015-2016 Vittorio Romeo
// License: Academic Free License ("AFL") v. 3.0
// AFL License page: http://opensource.org/licenses/AFL-3.0
// http://vittorioromeo.info | vittorio.romeo@outlook.com

#pragma once

#include <vrm/core/assert.hpp>

/// @macro Runtime assertion. (no message)
#define ORIZZONTE_ASSERT_NS(...) assert(__VA_ARGS__)

/// @macro Runtime assertion.
#define ORIZZONTE_ASSERT(...) VRM_CORE_ASSERT(__VA_ARGS__)

/// @macro Runtime assertion of a binary operation.
#define ORIZZONTE_ASSERT_OP(...) VRM_CORE_ASSERT_OP(__VA_ARGS__)

/// @macro Static assertion. Does not require a message.
#define ORIZZONTE_S_ASSERT(...) VRM_CORE_STATIC_ASSERT_NM(__VA_ARGS__)

/// @macro Static assertion with message.
#define ORIZZONTE_S_ASSERT_M(...) VRM_CORE_STATIC_ASSERT(__VA_ARGS__)

/// @macro Statically asserts the passed variadic arguments, after wrapping them
/// in `decltype(...){}`.
#define ORIZZONTE_S_ASSERT_DT(...) ORIZZONTE_S_ASSERT(decltype(__VA_ARGS__){})
