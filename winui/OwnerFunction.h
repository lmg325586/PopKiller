#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

// 携带注册者（owner）指针的可调用对象包装：
// 用于在多个注册方（App/MainWindow 常驻管理层与各设置页实例）之间精确识别
// "回调是谁注册的"，从而让页面析构时只清除自己注册的回调、不误伤常驻回调。
//
// 与 std::function 不同，本类型支持捕获型 lambda 的所有者识别：
// 注册时将 owner 指针作为第一个参数注入被包装的可调用对象
// （兼容 f(owner, args...) 与无参 f(owner) 两种签名），因此页面注册的
// [weakThis](bool ok, std::wstring msg){...} 形式同样能被 ClearCallbackIfOwnedBy 精确匹配。
// 定义于全局命名空间，供 PopupBlocker.h / MainWindow.xaml.h / PopupBlockerPage.xaml.h 共用。
template <typename Sig>
class owner_function;

template <typename R, typename... Args>
class owner_function<R(Args...)>
{
public:
    using invoker = R(*)(const owner_function*, Args...);

    owner_function() noexcept = default;
    owner_function(std::nullptr_t) noexcept {}

    // 普通可调用对象（函数指针 / 无捕获 lambda）：注册时必须以 owner(void*) 作首参，
    // 例如 MainWindow 层的常驻回调 PersistentEnabledChanged(void* ctx)。
    template <typename F,
        typename = std::enable_if_t<
            !std::is_same_v<std::decay_t<F>, owner_function> &&
            !std::is_same_v<std::decay_t<F>, std::nullptr_t> &&
            std::is_invocable_r_v<R, std::decay_t<F>&, void*, Args...>>>
    owner_function(F&& f, const void* owner = nullptr)
        : m_owner(owner), m_state(std::make_shared<state_model<std::decay_t<F>>>(std::forward<F>(f)))
    {
        m_invoke = &do_invoke<std::decay_t<F>>;
    }

    // 带显式 owner 的构造（语义同上，便于调用点自文档化）。
    template <typename F>
    static owner_function with_owner(F&& f, const void* owner)
    {
        return owner_function(std::forward<F>(f), owner);
    }

    explicit operator bool() const noexcept { return static_cast<bool>(m_state); }

    const void* owner() const noexcept { return m_owner; }

    R operator()(Args... args) const
    {
        if (m_state) return m_invoke(this, std::forward<Args>(args)...);
        if constexpr (!std::is_void_v<R>) return R{};
    }

private:
    struct state_base { virtual ~state_base() = default; };

    template <typename F>
    struct state_model final : state_base
    {
        explicit state_model(F f) : fn(std::move(f)) {}
        F fn;
    };

    template <typename F>
    static R do_invoke(const owner_function* self, Args... args)
    {
        auto* m = static_cast<state_model<std::decay_t<F>>*>(self->m_state.get());
        if constexpr (std::is_void_v<R>)
            m->fn(const_cast<void*>(self->m_owner), std::forward<Args>(args)...);
        else
            return m->fn(const_cast<void*>(self->m_owner), std::forward<Args>(args)...);
    }

    const void* m_owner{ nullptr };
    std::shared_ptr<state_base> m_state;
    invoker m_invoke{ nullptr };

    template <typename F>
    friend void owner_bind(owner_function<R(Args...)>& slot, F&& f, const void* owner);
};

// 注册辅助：把可调用对象 f 连同其所有者指针 owner 存入槽位 slot。
// f 需接受 (void* owner, args...)；lambda 形参写 void* 时可省略实参转换。
template <typename R, typename... Args, typename F>
inline void owner_bind(owner_function<R(Args...)>& slot, F&& f, const void* owner)
{
    using fn_t = owner_function<R(Args...)>;
    using decay_f = std::decay_t<F>;

    fn_t tmp;
    tmp.m_owner = owner;
    tmp.m_state = std::make_shared<typename fn_t::template state_model<decay_f>>(std::forward<F>(f));
    tmp.m_invoke = &fn_t::template do_invoke<decay_f>;
    slot = std::move(tmp);
}
