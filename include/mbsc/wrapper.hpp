#pragma once

#include <utility>

template <typename T>
struct ErrorPolicy;

template <typename T>
class Wrapper {
public:
    explicit Wrapper(T& ctx) : ctx_(&ctx) {}

    [[nodiscard]] T* ctx() const noexcept { return ctx_; }

    template <typename Func, typename... Args>
    auto call(Func f, Args&&... args) {
        auto rc = invoke(f, std::forward<Args>(args)...);
        ErrorPolicy<T>::check(rc, ctx_);
        return rc;
    }

    template <typename Func, typename... Args>
    auto invoke(Func f, Args&&... args) noexcept {
        return f(ctx_, std::forward<Args>(args)...);
    }

protected:
    T* ctx_ = nullptr;
};
