# SDK MicroPython module

The upstream MicroPython module's `cmake-ext` and `kconfig-ext` settings select
this directory through `sdk/modules/modules.cmake`.

- `port/`: SDK Zephyr port, enabled by `CONFIG_MICROPYTHON`.
- `light/`: existing shell-based implementation, enabled by
  `CONFIG_MICROPYTHON_LIGHT`.

Enable only one implementation. The interpreter core remains in the west
project `modules/lib/micropython` (`ZEPHYR_MICROPYTHON_MODULE_DIR`).

## Application integration

Build `sdk/app`, not this directory. For example, from the west workspace:

```sh
west build -b stm32n6570_dk --shield st_b_cams_imx_mb1854 sdk/app \
  -d builds/stm32n6570_dk/app
```

With `CONFIG_MICROPYTHON_STANDALONE=n` (the default), the full port registers
its own interpreter thread in `port/src/zephyr_start.c`.
The stack size is `CONFIG_MICROPYTHON_THREAD_STACK_SIZE` (4736 bytes by default).
USB initialization remains in `sdk/app/src/main.c`.

Relative `CONFIG_MICROPY_FROZEN_MANIFEST` paths are resolved against
`APPLICATION_SOURCE_DIR`. The app's `boards/manifest.py` therefore stays in the
app, while `$(PORT_DIR)/modules` now resolves to this module's `port/modules`.
Generated MicroPython headers remain in the build root's `genhdr/` directory.

## Standalone application

Set `CONFIG_MICROPYTHON_STANDALONE=y` in an application that does not define
`main()`. The SDK port provides `main()` and uses `CONFIG_MAIN_STACK_SIZE`
instead of the separate interpreter thread's stack. This does not make this
module directory a top-level Zephyr application.

For example:

```conf
CONFIG_MICROPYTHON=y
CONFIG_MICROPYTHON_STANDALONE=y
CONFIG_CONSOLE_SUBSYS=y
CONFIG_CONSOLE_GETCHAR=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_MAIN_STACK_SIZE=8192
```

The standalone REPL uses `micropy,console` when present, otherwise
`zephyr,console`. The UART must not also be used by the serial shell. Disable
that shell backend when using its UART for the standalone REPL. USB consoles
still require the application to arrange USB device initialization.

In the embedded UART mode (`CONFIG_CONSOLE_SUBSYS=y`), explicitly provide a
separate `micropy,console`. Both modes initialize the selected console once via
`mp_console_init()`; they do not call the generic `console_init()`. MicroPython
`print()` and REPL input use that selected device; Zephyr logging continues to
use its configured backends.

## Preserved upstream files

The full port's `mpconfigport.h` still references upstream `ports/zephyr/` files
through the MicroPython `*_INCLUDEFILE` macros. The matching local source copies
were identical to the current upstream checkout when relocated; those copies
are not separate compilation units. Changes to these bindings must account for
that include routing.

`port/src/usbd.c` is retained but is not compiled; the app owns USB setup.
`port/prj.conf`, `port/prj_minimal.conf`, and `port/thread.conf` are retained
upstream configuration examples and are not loaded automatically by `sdk/app`.
`port/shellcmd.c` is the existing console-selection fallback. Its runtime
behavior is outside the scope of this source relocation.

The upstream usage notes and license notices are retained in `port/README.md`
and the source files.
