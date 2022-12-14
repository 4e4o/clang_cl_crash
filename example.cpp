#include <coroutine>
#include <optional>

#include <iostream>
#include <thread>

#include <chrono>
#include <queue>
#include <vector>

// basic coroutine single-threaded async task example

template<typename T>
struct task_promise_type;

// simple single-threaded timer for coroutines
void submit_timer_task(std::coroutine_handle<> handle, std::chrono::seconds timeout);

template<typename T>
struct task;

template<typename T>
struct task_promise_type
{
    // value to be computed
    // when task is not completed (coroutine didn't co_return anything yet) value is empty
    std::optional<T> value;

    // corouine that awaiting this coroutine value
    // we need to store it in order to resume it later when value of this coroutine will be computed
    std::coroutine_handle<> awaiting_coroutine;

    // task is async result of our coroutine
    // it is created before execution of the coroutine body
    // it can be either co_awaited inside another coroutine
    // or used via special interface for extracting values (is_ready and get)
    task<T> get_return_object();

    // there are two kinds of coroutines:
    // 1. eager - that start its execution immediately
    // 2. lazy - that start its execution only after 'co_await'ing on them
    // here I used eager coroutine task
    // eager: do not suspend before running coroutine body
    std::suspend_never initial_suspend()
    {
        return {};
    }

    // store value to be returned to awaiting coroutine or accessed through 'get' function
    void return_value(T val)
    {
        value = std::move(val);
    }

    void unhandled_exception()
    {
        // alternatively we can store current exeption in std::exception_ptr to rethrow it later
        std::terminate();
    }

    // when final suspend is executed 'value' is already set
    // we need to suspend this coroutine in order to use value in other coroutine or through 'get' function
    // otherwise promise object would be destroyed (together with stored value) and one couldn't access task result
    // value
    auto final_suspend() noexcept
    {
        // if there is a coroutine that is awaiting on this coroutine resume it
        struct transfer_awaitable
        {
            std::coroutine_handle<> awaiting_coroutine;

            // always stop at final suspend
            bool await_ready() noexcept
            {
                return false;
            }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<task_promise_type> h) noexcept
            {
                // resume awaiting coroutine or if there is no coroutine to resume return special coroutine that do
                // nothing
                return this->awaiting_coroutine ? this->awaiting_coroutine : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        return transfer_awaitable{awaiting_coroutine};
    }

    // there are multiple ways to add co_await into coroutines
    // I used `await_transform`

    // use `co_await std::chrono::seconds{n}` to wait specified amount of time
    auto await_transform(std::chrono::seconds duration)
    {
        struct timer_awaitable
        {
            std::chrono::seconds duration;
            // always suspend
            bool await_ready()
            {
                return false;
            }

            // h is a handler for current coroutine which is suspended
            void await_suspend(std::coroutine_handle<task_promise_type> h)
            {
                // submit suspended coroutine to be resumed after timeout
                submit_timer_task(h, this->duration);
            }
            void await_resume() {}
        };

        return timer_awaitable{duration};
    }

    // also we can await other task<T>
    template<typename U>
    auto await_transform(task<U>& task)
    {
        if (!task.handle) {
            throw std::runtime_error("coroutine without promise awaited");
        }
        if (task.handle.promise().awaiting_coroutine) {
            throw std::runtime_error("coroutine already awaited");
        }

        struct task_awaitable
        {
            std::coroutine_handle<task_promise_type<U>> handle;

            // check if this task already has value computed
            bool await_ready()
            {
                return handle.promise().value.has_value();
            }

            // h - is a handle to coroutine that calls co_await
            // store coroutine handle to be resumed after computing task value
            void await_suspend(std::coroutine_handle<> h)
            {
                handle.promise().awaiting_coroutine = h;
            }

            // when ready return value to a consumer
            auto await_resume()
            {
                return std::move(*(handle.promise().value));
            }
        };

        return task_awaitable{task.handle};
    }
};

template<typename T>
struct task
{
    // declare promise type
    using promise_type = task_promise_type<T>;

    task(std::coroutine_handle<promise_type> handle) : handle(handle) {}

    task(task&& other) : handle(std::exchange(other.handle, nullptr)) {}

    task& operator=(task&& other)
    {
        if (handle) {
            handle.destroy();
        }
        handle = other.handle;
    }

    ~task()
    {
        if (handle) {
            handle.destroy();
        }
    }

    // interface for extracting value without awaiting on it

    bool is_ready() const
    {
        if (handle) {
            return handle.promise().value.has_value();
        }
        return false;
    }

    T get()
    {
        if (handle) {
            return std::move(*handle.promise().value);
        }
        throw std::runtime_error("get from task without promise");
    }

    std::coroutine_handle<promise_type> handle;
};

template<typename T>
task<T> task_promise_type<T>::get_return_object()
{
    return {std::coroutine_handle<task_promise_type>::from_promise(*this)};
}

// simple timers

// stored timer tasks
struct timer_task
{
    std::chrono::steady_clock::time_point target_time;
    std::coroutine_handle<> handle;
};

// comparator
struct timer_task_before_cmp
{
    bool operator()(const timer_task& left, const timer_task& right) const
    {
        return left.target_time > right.target_time;
    }
};

std::priority_queue<timer_task, std::vector<timer_task>, timer_task_before_cmp> timers;

void submit_timer_task(std::coroutine_handle<> handle, std::chrono::seconds timeout)
{
    timers.push(timer_task{std::chrono::steady_clock::now() + timeout, handle});
}

// timer loop
void loop()
{
    while (!timers.empty()) {
        auto& timer = timers.top();
        // if it is time to run a coroutine
        if (timer.target_time < std::chrono::steady_clock::now()) {
            auto handle = timer.handle;
            timers.pop();
            handle.resume();
        } else {
            std::this_thread::sleep_until(timer.target_time);
        }
    }
}

// example

using namespace std::chrono_literals;

task<int> wait_n(int n)
{
    std::cout << "before wait " << n << '\n';
    co_await std::chrono::seconds(n);
    std::cout << "after wait " << n << '\n';
    co_return n;
}

task<int> test()
{
    for (auto c : "hello world\n") {
        std::cout << c;
        co_await 1s;
    }

    std::cout << "test step 1\n";
    auto w3 = wait_n(3);
    std::cout << "test step 2\n";
    auto w2 = wait_n(2);
    std::cout << "test step 3\n";
    auto w1 = wait_n(1);
    std::cout << "test step 4\n";
    auto r = co_await w2 + co_await w3;
    std::cout << "awaiting already computed coroutine\n";
    co_return co_await w1 + r;
}

// main can't be a coroutine and usually need some sort of looper (io_service or timer loop in this example )
int main()
{
    // do something

    auto result = test();

    // execute deferred coroutines
    loop();

    std::cout << "result: " << result.get();
}
