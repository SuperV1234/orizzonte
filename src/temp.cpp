#include <type_traits>
#include <memory>
#include <utility>
#include <cassert>
#include <future>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <random>
#include <chrono>

#include "../include/orizzonte.hpp"

using namespace orizzonte::utility;

template <typename F, typename... Ts, std::size_t... Is>
void enumerate_args_impl(std::index_sequence<Is...>, F&& f, Ts&&... xs)
{
    (f(std::integral_constant<std::size_t, Is>{}, FWD(xs)), ...);
}

template <typename F, typename... Ts>
void enumerate_args(F&& f, Ts&&... xs)
{
    enumerate_args_impl(std::index_sequence_for<Ts...>{}, FWD(f), FWD(xs)...);
}

template <typename T>
struct type_wrapper
{
    using type = T;
};

template <typename T>
inline constexpr type_wrapper<T> type_wrapper_v{};

template <typename T>
using unwrap_type = typename std::decay_t<T>::type;

class bool_latch
{
private:
    std::condition_variable _cv;
    std::mutex _mtx;
    bool _finished{false};

public:
    void count_down()
    {
        std::scoped_lock lk{_mtx};
        _finished = true;
        _cv.notify_all();
    }

    void wait()
    {
        std::unique_lock lk{_mtx};
        _cv.wait(lk, [this]{ return _finished; });
    }
};

template <typename Parent, typename F>
class node;

template <typename Parent, typename... Fs>
class when_all;

class root
{
    template <typename>
    friend class node_base;

public:
    // The `root` produces `nothing`.
    using output_type = nothing;

private:
    // When we are at the `root`, we cannot go "up" the chain anymore.
    // Therefore we being going "down".
    template <typename Scheduler, typename Child, typename... Children>
    void walk_up(Scheduler&& s, Child& c, Children&... cs) &
    {
        c.execute(s, nothing{}, cs...);
    }
};

template <typename Parent>
class child_of : public Parent
{
public:
    using input_type = typename Parent::output_type;

protected:
    template <typename ParentFwd>
    child_of(ParentFwd&& p) : Parent{FWD(p)}
    {
    }

    auto& as_parent() noexcept
    {
        return static_cast<Parent&>(*this);
    }
};

template <typename Derived>
class node_base
{
private:
    auto& as_derived() noexcept
    {
        return static_cast<Derived&>(*this);
    }

public:
    template <typename... FThens>
    auto then(FThens&&... f_thens) &&
    {
        if constexpr(sizeof...(FThens) == 1)
        {
            return node{std::move(as_derived()), FWD(f_thens)...};
        }
        else
        {
            return when_all{std::move(as_derived()), FWD(f_thens)...};
        }
    }

    template <typename Scheduler, typename... Children>
    void walk_up(Scheduler&& s, Children&... cs) &
    {
        as_derived().as_parent().walk_up(s, as_derived(), cs...);
    }

    template <typename Scheduler>
    decltype(auto) wait_and_get(Scheduler&& s) &&
    {
        typename Derived::output_type out;
        bool_latch l;

        auto f = std::move(as_derived()).then([&](auto&&... x)
        {
            ((out = FWD(x)), ...);
            l.count_down();
        });

        f.walk_up(s);

        l.wait();
        return out;
    }
};

template <typename Parent, typename F>
class node : private child_of<Parent>, private F,
             public node_base<node<Parent, F>>
{
public:
    using typename child_of<Parent>::input_type;
    using output_type = result_of_ignoring_nothing_t<F&, input_type>;

    template <typename ParentFwd, typename FFwd>
    node(ParentFwd&& p, FFwd&& f) : child_of<Parent>{FWD(p)}, F{FWD(f)}
    {
    }

    using crtp_base_type = node_base<node<Parent, F>>;
    friend crtp_base_type;

    using crtp_base_type::then;
    using crtp_base_type::walk_up;
    using crtp_base_type::wait_and_get;

private:
    auto& as_f() noexcept
    {
        return static_cast<F&>(*this);
    }

public:
    template <typename Scheduler, typename Result>
    void execute(Scheduler&&, Result&& r) &
    {
        call_ignoring_nothing(as_f(), FWD(r));
    }

    template <typename Scheduler, typename Result, typename Child, typename... Children>
    void execute(Scheduler&& s, Result&& r, Child& c, Children&... cs) &
    {
        // `r` doesn't need to be stored inside the node here as it is used
        // to synchronously invoke `as_f()`.
        c.execute(s, call_ignoring_nothing(as_f(), FWD(r)), cs...);
    }

};

template <typename ParentFwd, typename FFwd>
node(ParentFwd&&, FFwd&&) -> node<std::decay_t<ParentFwd>, std::decay_t<FFwd>>;

template <typename Parent>
class schedule : private child_of<Parent>,
                 public node_base<schedule<Parent>>
{
public:
    using typename child_of<Parent>::input_type;
    using output_type = nothing;

    template <typename ParentFwd>
    schedule(ParentFwd&& p) : child_of<Parent>{FWD(p)} { }

    using crtp_base_type = node_base<schedule<Parent>>;
    friend crtp_base_type;

    using crtp_base_type::then;
    using crtp_base_type::walk_up;
    using crtp_base_type::wait_and_get;

    template <typename Scheduler, typename Result, typename Child, typename... Children>
    void execute(Scheduler&& s, Result&&, Child& c, Children&... cs) &
    {
        s([&]{ c.execute(s, nothing{}, cs...); });
    }
};

template <typename ParentFwd>
schedule(ParentFwd&&) -> schedule<std::decay_t<ParentFwd>>;

template <typename T>
struct movable_atomic : std::atomic<T>
{
    using std::atomic<T>::atomic;
    movable_atomic(movable_atomic&& rhs) : std::atomic<T>{rhs.load()}
    {
    }
};

template <typename T>
struct tuple_of_nothing_to_empty;

template <typename T>
struct tuple_of_nothing_to_empty<std::tuple<T>>
{
    using type = std::tuple<T>;
};

template <>
struct tuple_of_nothing_to_empty<std::tuple<nothing>>
{
    using type = std::tuple<>;
};

template <typename T>
using tuple_of_nothing_to_empty_t = typename tuple_of_nothing_to_empty<T>::type;


template <typename T>
struct adapt_tuple_of_nothing;

template <typename... Ts>
struct adapt_tuple_of_nothing<std::tuple<Ts...>>
{
    using type = decltype(
        std::tuple_cat(std::declval<tuple_of_nothing_to_empty_t<std::tuple<Ts>>>()...)
    );
};

template <typename T>
using adapt_tuple_of_nothing_t = typename adapt_tuple_of_nothing<T>::type;

template <typename Parent, typename... Fs>
class when_all : private child_of<Parent>, private Fs...,
                 public node_base<when_all<Parent, Fs...>>
{
public:
    using typename child_of<Parent>::input_type;
    using output_type = std::tuple<result_of_ignoring_nothing_t<Fs&, input_type&>...>;

    // TODO: should this be done, or should the tuple be applied to following node?
    // using output_type = adapt_tuple_of_nothing_t<
    //     std::tuple<result_of_ignoring_nothing_t<Fs&, input_type&>...>
    // >;

private:
    // TODO: the size of the entire computation might grow by a lot. Is it possible to reuse this space for multiple nodes?
    movable_atomic<int> _left{sizeof...(Fs)};
    output_type _out;
    std::aligned_storage_t<sizeof(input_type), alignof(input_type)> _input_buf;

public:
    template <typename ParentFwd, typename... FFwds>
    when_all(ParentFwd&& p, FFwds&&... fs) : child_of<Parent>{FWD(p)}, Fs{FWD(fs)}...
    {
    }

    using crtp_base_type = node_base<when_all<Parent, Fs...>>;
    friend crtp_base_type;

    using crtp_base_type::then;
    using crtp_base_type::walk_up;
    using crtp_base_type::wait_and_get;

public:
    template <typename Scheduler, typename Result>
    void execute(Scheduler&& s, Result&& r) &
    {
        // TODO: what if `Result` is an lvalue reference?
        new (&_input_buf) std::decay_t<Result>(FWD(r));

        enumerate_args([&](auto i, auto t)
        {
            auto do_computation = [&]
            {
                call_ignoring_nothing(static_cast<unwrap_type<decltype(t)>&>(*this),
                                      reinterpret_cast<Result&>(_input_buf));

                if(_left.fetch_sub(1) == 1)
                {
                    // TODO: make sure this destruction is correct, probably should not destroy if storing lvalue reference
                    reinterpret_cast<input_type&>(_input_buf).~input_type();
                }
            };

            if constexpr(i == sizeof...(Fs) - 1)
            {
                do_computation();
            }
            else
            {
                // `do_computation` has to be moved here as it will die at the end of the `enumerate_args` lambda scope.
                s([g = std::move(do_computation)]{ g(); });
            }
        }, type_wrapper_v<Fs>...);
    }

    template <typename Scheduler, typename Result, typename Child, typename... Children>
    void execute(Scheduler&& s, Result&& r, Child& c, Children&... cs) &
    {
        // This is necessary as `r` only lives as long as `execute` is active on the call stack.
        // Computations might still be active when `execute` ends, even if the last one is executed on the same thread.
        // This is because the scheduled computations might finish after the last one.
        // TODO: what if `Result` is an lvalue reference?
        new (&_input_buf) std::decay_t<Result>(FWD(r));

        enumerate_args([&](auto i, auto t)
        {
            auto do_computation = [&]
            {
                std::get<decltype(i){}>(_out) =
                    call_ignoring_nothing(static_cast<unwrap_type<decltype(t)>&>(*this),
                                          reinterpret_cast<Result&>(_input_buf));

                if(_left.fetch_sub(1) == 1)
                {
                    // TODO: make sure this destruction is correct, probably should not destroy if storing lvalue reference
                    reinterpret_cast<input_type&>(_input_buf).~input_type();
                    c.execute(s, _out, cs...); // TODO: apply the tuple here to pass multiple arguments
                }
            };

            if constexpr(i == sizeof...(Fs) - 1)
            {
                do_computation();
            }
            else
            {
                // `do_computation` has to be moved here as it will die at the end of the `enumerate_args` lambda scope.
                s([g = std::move(do_computation)]{ g(); });
            }
        }, type_wrapper_v<Fs>...);
    }
};

template <typename ParentFwd, typename... FFwds>
when_all(ParentFwd&&, FFwds&&...) -> when_all<std::decay_t<ParentFwd>, std::decay_t<FFwds>...>;

template <typename... Fs>
auto initiate(Fs&&... fs)
{
    return schedule{root{}}.then(FWD(fs)...);
}

struct world_s_best_thread_pool
{
    template <typename F>
    void operator()(F&& f)
    {
        std::thread{FWD(f)}.detach();
    }
};

std::atomic<int> ctr = 0;
struct sctr { sctr(){ ++ctr; } ~sctr(){ --ctr; } };

void fuzzy()
{
    std::mutex mtx;
    std::random_device rd;
    std::default_random_engine re(rd());

    const auto rndint = [&](int min, int max)
    {
        std::scoped_lock l{mtx};
        return std::uniform_int_distribution<int>{min, max - 1}(re);
    };

    const auto rndsleep = [&]
    {
        std::this_thread::sleep_for(std::chrono::nanoseconds{rndint(0, 100)});
    };

    for(int i = 0; i < 1000; ++i)
    {
        auto f = initiate([&]{ rndsleep(); return 1; },
                          [&]{ rndsleep(); return 2; },
                          [&]{ rndsleep(); return 3; })
            .then([&](auto t)
            {
                rndsleep();
                auto [a, b, c] = t;
                assert(a + b + c == 1 + 2 + 3);
                return 0;
            },
            [&](auto t)
            {
                rndsleep();
                auto [a, b, c] = t;
                assert(a + b + c == 1 + 2 + 3);
                return 1;
            }).then([](auto t){ auto [a, b] = t; assert(a+b==1); return std::string{"hello"}; },
                    [](auto t){ auto [a, b] = t; assert(a+b==1); return std::string{"world"}; })
            .then([](auto y){ auto [s0, s1] = y; assert(s0 + s1 == "helloworld"); });

        std::move(f).wait_and_get(world_s_best_thread_pool{});
    }


    for(int i = 0; i < 1000; ++i)
    {
        assert(ctr == 0);
        auto f = initiate([]{ return std::make_shared<sctr>(); })
                .then([](auto&){  assert(ctr == 1); },
                      [](auto&){ assert(ctr == 1); })
                .then([](auto){ assert(ctr == 0); });

        std::move(f).wait_and_get(world_s_best_thread_pool{});
        assert(ctr == 0);
    }
}



int main()
{
    fuzzy();

    {
        auto f = initiate([]{ return 1; });
        assert(std::move(f).wait_and_get(world_s_best_thread_pool{}) == 1);
    }


    {
        auto f = initiate([]{ return 1; })
             .then([](int x){ return x + 1; });

        assert(std::move(f).wait_and_get(world_s_best_thread_pool{}) == 2);
    }

    {
        auto f = initiate([]{ return 1; })
             .then([](int x){ return x + 1; })
             .then([](int x){ return x + 1; });

        assert(std::move(f).wait_and_get(world_s_best_thread_pool{}) == 3);
    }

    {
        auto f = initiate([]{ return 1; })
                .then([](int x){ return x + 1; })
                .then([](int){ std::cout << "void!\n"; });

        std::move(f).wait_and_get(world_s_best_thread_pool{});
    }

    {
        auto f = initiate([]{ return 1; }, []{ return 2; });

        assert(std::move(f).wait_and_get(world_s_best_thread_pool{}) == (std::tuple{1, 2}));
    }

    {
        auto f = initiate([]{ return 1; }, []{ return 1; })
                .then([](auto t){ auto [a, b] = t; return a + b; });

        assert(std::move(f).wait_and_get(world_s_best_thread_pool{}) == 2);
    }

    auto f2 = initiate([]{ std::cout << "A0\n"; return 1; },
                       []{ std::cout << "A1\n"; return 2; })
       .then([](auto t)
    {
        auto [a, b] = t;
        assert(a + b == 3);
        return a + b;
    }).then([](auto x){ assert(x == 3); std::cout << x << " C0\n"; return std::string{"hello"}; },
            [](auto x){ assert(x == 3); std::cout << x << " C1\n"; return std::string{"world"}; })
      .then([](auto y){ auto [s0, s1] = y; assert(s0 == "hello"); assert(s1 == "world"); std::cout << s0 << " " << s1 << "\n"; });

    std::move(f2).wait_and_get(world_s_best_thread_pool{});

    auto f3 = initiate([]{}).then([]{});
    std::move(f3).wait_and_get(world_s_best_thread_pool{});

    auto f4 = initiate([]{},[]{}).then([](auto){});
    std::move(f4).wait_and_get(world_s_best_thread_pool{});

    auto f5 = initiate([]{ std::cout << "A0\n"; return 1; },
                       []{ std::cout << "A1\n"; return 2; })
       .then([](auto t)
    {
        auto [a, b] = t;
        assert(a + b == 3);
        return a + b;
    }).then([](auto x){ assert(x == 3); std::cout << x << " C0\n"; return 2; },
            [](auto x){ assert(x == 3); std::cout << x << " C1\n"; return 3; })
      .then([](auto t){ auto [a, b] = t; auto x = a+b; assert(x == 5); std::cout << x << " C2\n"; return 4; },
            [](auto t){ auto [a, b] = t; auto x = a+b; assert(x == 5); std::cout << x << " C3\n"; return 5; })
      .then([](auto y){ auto [a, b] = y; assert(a+b == 9); });

    std::move(f5).wait_and_get(world_s_best_thread_pool{});

    std::cout.flush();
    return 0;
}
