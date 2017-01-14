#pragma once

// TODO: to vrm_core?

#include <type_traits>
#include <functional>
#include "tuple.hpp"

namespace orizzonte::impl
{
    namespace detail
    {
        template <typename T>
        class fwd_capture_tuple : private std::tuple<T>
        {
        private:
            using decay_element_type = std::decay_t<T>;
            using base_type = std::tuple<T>;

        protected:
            constexpr auto& as_tuple() noexcept
            {
                return static_cast<base_type&>(*this);
            }

            constexpr const auto& as_tuple() const noexcept
            {
                return static_cast<const base_type&>(*this);
            }

            template <typename TFwd>
            constexpr fwd_capture_tuple(TFwd&& x)
                noexcept(std::is_nothrow_constructible<base_type, decltype(x)>{})
                : base_type(FWD(x))
            {
            }

        public:
            constexpr auto& get() & noexcept
            {
                return std::get<0>(as_tuple());
            }

            constexpr const auto& get() const & noexcept
            {
                return std::get<0>(as_tuple());
            }

            constexpr auto get() && noexcept(std::is_move_constructible<decay_element_type>{})
            {
                return std::move(std::get<0>(as_tuple()));
            }
        };
    }

    template <typename T>
    class fwd_capture_wrapper : public detail::fwd_capture_tuple<T>
    {
    private:
        using base_type = detail::fwd_capture_tuple<T>;

    public:
        template <typename TFwd>
        constexpr fwd_capture_wrapper(TFwd&& x)
            noexcept(std::is_nothrow_constructible<base_type, decltype(x)>{})
            : base_type(FWD(x))
        {
        }
    };

    template <typename T>
    class fwd_copy_capture_wrapper : public detail::fwd_capture_tuple<T>
    {
    private:
        using base_type = detail::fwd_capture_tuple<T>;

    public:
        // No `FWD` is intentional, to force a copy if `T` is not an lvalue reference.
        template <typename TFwd>
        constexpr fwd_copy_capture_wrapper(TFwd&& x)
            noexcept(std::is_nothrow_constructible<base_type, decltype(x)>{})
            : base_type(x)
        {
        }
    };

    template <typename T>
    constexpr auto fwd_capture(T&& x)
        noexcept(noexcept(fwd_capture_wrapper<T>(FWD(x))))
    {
        return fwd_capture_wrapper<T>(FWD(x));
    }

    template <typename T>
    constexpr auto fwd_copy_capture(T&& x)
        noexcept(noexcept(fwd_copy_capture_wrapper<T>(FWD(x))))
    {
        return fwd_copy_capture_wrapper<T>(FWD(x));
    }
}

#define FWD_CAPTURE(...) orizzonte::impl::fwd_capture(FWD(__VA_ARGS__))

#define FWD_COPY_CAPTURE(...) \
    orizzonte::impl::fwd_copy_capture(FWD(__VA_ARGS__))

namespace orizzonte::impl
{
    template <typename... Ts>
    constexpr auto fwd_capture_as_tuple(Ts&&... xs)
        noexcept(noexcept(std::make_tuple(FWD_CAPTURE(xs)...)))
    {
        return std::make_tuple(FWD_CAPTURE(xs)...);
    }

    template <typename... Ts>
    constexpr auto fwd_copy_capture_as_tuple(Ts&&... xs)
        noexcept(noexcept(std::make_tuple(FWD_COPY_CAPTURE(xs)...)))
    {
        return std::make_tuple(FWD_COPY_CAPTURE(xs)...);
    }

    template <typename TF, typename TFwdCapture>
    constexpr decltype(auto) apply_fwd_capture(TF&& f, TFwdCapture&& fc)
        // TODO: noexcept
    {
        return orizzonte::impl::apply([&f](auto&&... xs) mutable -> decltype(
                                          auto) { return f(FWD(xs).get()...); },
            FWD(fc));
    }
}

#define FWD_CAPTURE_PACK_AS_TUPLE(...) \
    orizzonte::impl::fwd_capture_as_tuple(FWD(__VA_ARGS__)...)

#define FWD_COPY_CAPTURE_PACK_AS_TUPLE(...) \
    orizzonte::impl::fwd_copy_capture_as_tuple(FWD(__VA_ARGS__)...)
