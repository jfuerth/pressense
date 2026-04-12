#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Unity test transport for picosdk framework
// Uses stdio (USB serial) for test output

inline void unittest_uart_begin() {
    // stdio_init_all() is already called in main()
}

inline void unittest_uart_putchar(char c) {
    putchar(c);
}

inline void unittest_uart_flush() {
    fflush(stdout);
}

inline void unittest_uart_end() {
    // No cleanup needed
}

#ifdef __cplusplus
}
#endif
