#include <coroutine>
//#include <experimental/coroutine>

#include <stdio.h>

using namespace std;

class my_awaitable_promise;
class my_awaitable;

static my_awaitable* g_awaitable_ptr = nullptr;

class my_awaitable {
public:
    my_awaitable_promise *pp;

    my_awaitable() {
        printf("my_awaitable() %p\n", this);
        g_awaitable_ptr = this;
    }

    my_awaitable(my_awaitable_promise* p) noexcept {
        pp = p;
//        printf("my_awaitable(my_awaitable_promise* p) %p\n", this);
    }

    my_awaitable(my_awaitable&& other) {
        if ((&other) != g_awaitable_ptr) {
            printf("BAAAAAAAAAAAAAD!!!! %p %p\n", &other, g_awaitable_ptr);
        } else {
            printf("GOOOOOOOOOOOOOD!!!! %p %p\n", &other, g_awaitable_ptr);
        }

//        printf("my_awaitable(my_awaitable&& other) %p other=%p\n", this, &other);
    }

    ~my_awaitable() {
//        printf("~my_awaitable() %p\n", this);
    }

    void await_suspend(coroutine_handle<void>) {
    }

    void return_void() {
    }

    void await_resume() {
    }

    bool await_ready() const noexcept {
        return false;
    }
};

class my_awaitable_promise {
public:
    coroutine_handle<void> coro_ = nullptr;

    my_awaitable get_return_object() noexcept {
        this->coro_ = coroutine_handle<my_awaitable_promise>::from_promise(*this);
        return my_awaitable(this);
    };

    auto await_transform(my_awaitable a) const {
        return a;
    }

    my_awaitable_promise() noexcept {
//        printf("my_awaitable_promise::constr %p\n", this);
    }

    ~my_awaitable_promise() {
//        printf("~my_awaitable_promise %p\n", this);
    }

    void unhandled_exception() {
    }

    void return_void() {
    }

    auto initial_suspend() noexcept {
        return suspend_always();
    }

    auto final_suspend() noexcept {
        struct result {
            bool await_ready() const noexcept {
                return false;
            }

            void await_suspend(coroutine_handle<void>) noexcept {
            }

            void await_resume() const noexcept {
            }
        };

        return result{};
    }
};


namespace std {

template <typename... Args>
struct coroutine_traits<my_awaitable, Args...> {
    typedef my_awaitable_promise promise_type;
};
}

int main(int argc, char **argv) {
    auto bb = []() -> my_awaitable {
        co_await my_awaitable();
        co_return;
    };

    auto cc = bb();
    cc.pp->coro_.resume();

    return 0;
}
