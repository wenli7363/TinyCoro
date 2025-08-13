// file: coro.cpp
// compile: g++ coro.cpp -std=c++20 -fcoroutines -O3 -o coro
#include "coro.hpp"

task<int> add(int a, int b) { co_return a + b; }

task<> func() {
  auto result = co_await add(1, 2);
  std::cout << "result: " << result << std::endl;
}

int main(int argc, char const *argv[]) {
  auto h = func();
  h.resume();
  return 0;
}