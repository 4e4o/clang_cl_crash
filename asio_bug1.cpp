#include <boost/asio.hpp>
#include <boost/asio/deferred.hpp>

#include <iostream>
#include <iomanip>

using namespace boost::asio;

awaitable<void> run() {
    std::string line;
    posix::stream_descriptor in(co_await this_coro::executor, ::dup(STDIN_FILENO));

    auto op1 = async_read_until(in, dynamic_buffer(line), '\n', use_awaitable);

//#define BUUUUUG 1

#ifdef BUUUUUG
    // COMPILATION FAILS
    auto lambda1 = [op1 = std::forward<decltype(op1)>(op1)]() mutable -> awaitable<void> {
        co_return;
    };
#else
    // this version compiles
    auto lambda1 = []() mutable -> awaitable<void> {
        co_return;
    };
#endif

    auto deferredSpawn = co_spawn(co_await this_coro::executor, std::move(lambda1), deferred);
    co_await deferredSpawn(use_awaitable);
}

int main() {
    io_context ctx;
    co_spawn(ctx, run(), detached);
    ctx.run();
}

