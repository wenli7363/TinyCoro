// file: utils.hpp
#pragma once

#include <iostream>

// use optimize("O0") to avoid false rbp value
__attribute__((optimize("O0"))) void print_rbp(const char *func_name) {
  unsigned long long rbp_value;
  asm volatile("mov %%rbp, %0" : "=r"(rbp_value));
  std::cout << func_name << std::hex << std::showbase
            << " rbp value: " << rbp_value << std::dec << std::endl;
}

#define SHOW_ADDRESS(message, address)                                         \
  std::cout << message << ": " << std::hex << std::showbase << address         \
            << std::dec << std::endl

#ifdef HOOK_MEMORY
void *operator new(std::size_t size) {
  auto address = malloc(size);
  std::cout << "Allocating " << size << " bytes in address " << std::hex
            << std::showbase << address << std::dec << std::endl;
  return address;
}
#endif // HOOK_MEMORY