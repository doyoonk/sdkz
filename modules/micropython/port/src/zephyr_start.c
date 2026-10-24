/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Linaro Limited
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <zephyr/kernel.h>
#include "zephyr_getchar.h"

int real_main(void);
int mp_console_init(void);

static int micropython_start(void) {
    #ifdef CONFIG_MICROPY_GETCHAR_CONSOLE_SUBSYS
    int ret = mp_console_init();
    if (ret != 0) {
        printk("MicroPython console initialization failed (%d)\n", ret);
        return ret;
    }
    #else
    zephyr_getchar_init();
    #endif
    return real_main();
}

#if defined(CONFIG_MICROPYTHON_STANDALONE)
int main(void) {
    return micropython_start();
}
#else
static void _main(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    (void)micropython_start();
}

K_THREAD_DEFINE(micorpy, CONFIG_MICROPYTHON_THREAD_STACK_SIZE,
                _main, NULL, NULL, NULL, 7, 0, 0);
#endif
