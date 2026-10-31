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
  -d build/stm32n6570_dk/app
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

In embedded mode, explicitly provide `micropy,console`. The upstream
`CONFIG_MICROPY_GETCHAR_CONSOLE_SUBSYS` backend uses the SDK's dedicated tty
instance, initialized once by `mp_console_init()`. It does not call the generic
`console_init()`. MicroPython `print()` and REPL input use that selected device;
Zephyr logging continues to use its configured backends.

`CONFIG_MICROPY_GETCHAR_CONSOLE_DRIVER` is also supported. It requires
`CONFIG_UART_CONSOLE_DEBUG_SERVER_HOOKS=y`, `CONFIG_CONSOLE_SUBSYS=n`, and uses
`zephyr,console` through `zephyr_getchar_init()`. A different `micropy,console`
is rejected for this backend. Its receive buffer size must be a power of two
no greater than 65536 bytes. Neither backend may share its UART with the
serial shell.

## Preserved upstream files

The full port's `mpconfigport.h` still references upstream `ports/zephyr/` files
through the MicroPython `*_INCLUDEFILE` macros. The matching local source copies
were identical to the current upstream checkout when relocated; those copies
are not separate compilation units. Changes to these bindings must account for
that include routing.

`port/src/usbd.c` is retained but is not compiled; the app owns USB setup.
`port/prj.conf`, `port/prj_minimal.conf`, and `port/thread.conf` are retained
upstream configuration examples and are not loaded automatically by `sdk/app`.
`port/shellcmd.c` is retained as legacy source and is not compiled by the full
port. Use `CONFIG_MICROPYTHON_LIGHT` for the separate shell implementation.

The upstream usage notes and license notices are retained in `port/README.md`
and the source files.

## Upstream synchronization

The port includes the upstream Zephyr changes through MicroPython commit
`682e3f1cd71c87a766ffb8fe9799222100283d1c` (2026-10-23): explicit console backend
selection, TX retry on `-EAGAIN`, Thumb callable pointers, dynamic device
information, I2C frequency/deferred initialization, dynamic pinctrl and related
pin identifiers. The timer, display and regulator sources already matched.

The SDK retains its library build integration, standalone/thread entry points,
USB ownership, separate console, shared heap, libc printf, SPI controller
constant and existing socket configuration adaptation.

Optional APIs are enabled explicitly:

- `CONFIG_DEVICE_DT_METADATA=y` plus `CONFIG_MICROPY_DYNAMIC_DEVICE_INFOS=y`
  expose `zephyr.list_devices()` and device lookup by API/name/label.
- `CONFIG_MICROPY_RELATED_PINS=y` enables board-defined integer/string Pin IDs.
  Bindings are installed in `sdk/dts/bindings/micropython`.
- `CONFIG_MICROPY_DYNAMIC_PINCTRL=y` requires a matching pinmux representation,
  builder and generator settings. Upstream builders for RP2040/RP2350 and BFLB
  are in `port/dynamic_pinctrl`; upstream board examples and helper headers
  are available in `modules/lib/micropython/ports/zephyr/boards` in the
  west workspace. They are not copied into the SDK or automatically enabled
  for `sdk/app`.
  STM32 needs a platform-specific builder/pinmux implementation; this sync
  does not enable dynamic pinctrl on STM32N6.

QSTR regeneration depends on configuration, devicetree and generated pinmux
headers. Device metadata permits status-okay nodes without a linked driver;
accessing such a device reports an error rather than forcing a link reference.

The synchronized Pin API uses `Pin.OUT` for output only, adds `Pin.INOUT`,
and treats `Pin.OPEN_DRAIN` as a flag to combine with `Pin.OUT` or `Pin.INOUT`.
Level-triggered IRQ constants are also available. Review scripts that relied
on the former output-plus-input mode or standalone open-drain constant.
