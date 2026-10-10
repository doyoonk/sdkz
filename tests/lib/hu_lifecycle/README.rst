HU transport lifecycle tests
============================

Run on Linux with GCC, CMake and pthreads::

  cmake -S sdk/tests/lib/hu_lifecycle -B /tmp/hu-lifecycle -DCMAKE_BUILD_TYPE=Debug
  cmake --build /tmp/hu-lifecycle
  ctest --test-dir /tmp/hu-lifecycle --output-on-failure

The tests include the production server/UART/UDP source files. AddressSanitizer
and UndefinedBehaviorSanitizer detect invalid memory access and double frees.
Assertions remain enabled regardless of build type.

* Server: invalid arguments, allocation/driver/protocol initialization failures,
  blocked receive cancellation, draining a running command before destruction,
  and rejection of destruction from the command thread itself.
* UART: failed callback registration/RX initialization, duplicate ownership,
  receive/send cancellation, draining an active interrupt callback, delayed DMA
  completion, late callbacks after destruction and reopening, polling shutdown.
  A 1537-byte burst checks full RX buffer pause/resume and byte ordering while
  TX remains usable. Zero/error FIFO reads check callback progress, and a
  synchronized resume/stop race checks that shutdown leaves RX disabled.
* UDP: real POSIX loopback request/reply and cancellation of blocked reception.
* UDP sessions: production HU parsing over real loopback sockets, interleaved
  partial packets and CRCs, identical sequences, different IPs/ports, session
  limits, idle expiry/reuse, allocation failure recovery and teardown.
* Protocol: strict CRC syntax/matching for requests and responses, NAK header
  preservation, reset after invalid frames, receive buffer/argument limits,
  and recovery with another packet in the same input buffer.
* Flash commands: real ASCII85 decoding and HU parsing with a mock flash map;
  nonzero partition IDs, 1..256-byte writes, malformed sizes/offsets/data,
  inactive slot lookup, driver errors, erase and unsupported write protection.
  This suite runs with FLASH_HAS_EX_OP both enabled and disabled.
* STM32 write protection: the real Zephyr extension declarations with a mock
  controller verify partition sector masks, nonuniform page sizes, unchanged
  adjacent sectors, repeated requests, readback, driver errors and device
  isolation. Variants cover disabled page layout, disabled extended operations
  and write-protection disable prevention. No hardware option bytes are changed.
* Bounds: exact receive capacity with/without CRC, 64/65 arguments, bytewise
  reception/recovery, transmit trailer space, oversized/unterminated strings,
  hexadecimal byte encoding, long echoed headers and overflow NAKs. Guard
  values and callback/user pointers detect corruption within the parser object.

Small Zephyr mocks use pthreads and explicit synchronization gates. The UART
driver and FIFO are mocked; these tests do not validate real DMA, IRQ priority,
USB workqueue scheduling or Zephyr socket timing. The protocol mock uses the
protocol lifecycle interface and does not exercise packet parsing. The UDP
sessions test uses the real protocol header and implementation instead; its
clock offset advances idle expiry without waiting 30 seconds.
Flash tests never access board flash; they check recorded writes in host memory.
Firmware builds must separately check enabled UART API configurations.

The owner must serialize initialization/destruction and call destruction from
outside the HU command thread and UART callback workqueue. Destruction waits
for a running command to finish rather than aborting a thread holding locks.

UDP defaults to four IPv4 sender sessions and a 30-second idle timeout, controlled
by CONFIG_HU_UDP_MAX_SESSIONS and CONFIG_HU_UDP_SESSION_TIMEOUT_MS. A full session
table drops new senders without replacing existing partial packets. Each session
owns its response address; sequence values are never used for deduplication.
HU_APP selects NET_CONTEXT_RCVTIMEO and NET_CONTEXT_SNDTIMEO for Ethernet so
the native Zephyr socket backend supports the receive/send deadlines required
for cancellation. POSIX host sockets do not validate these Kconfig dependencies.

HU response builders reserve CRC/end-marker/NUL space and latch errors until
reset/ACK/NAK begins a new response. Buffer arguments must be NULL or the handle's
own tx_buffer; unknown-capacity external buffers are rejected with EINVAL.
Use hupacket_append_hex_bytes for binary payloads encoded as hexadecimal, rather
than writing into the response buffer directly. hupacket_send_buffer returns
EMSGSIZE on overflow and sends an EMSGSIZE NAK if the original header fits.
An unrepresentable header is dropped; a truncated ACK is never sent. After a
send attempt reaches the transport, reset the response before rebuilding it.

``wrprt <partition> <0|1>`` uses hardware protection only on the STM32 internal
flash controller, with CONFIG_FLASH_STM32_WRITE_PROTECT,
CONFIG_FLASH_EX_OP_ENABLED and CONFIG_FLASH_PAGE_LAYOUT enabled. It requires
whole sectors, changes only the selected partition's sector mask, and verifies
the mask returned by the driver. Already matching requests do not rewrite
option bytes. Disable prevention returns EACCES for an unlock request.
Other devices/configurations return ENOTSUP; in particular the STM32N6570-DK
external XSPI flash is unsupported. There is no software-only protection fallback
and no automatic reset. Hardware application/reset requirements remain those
of the selected flash driver and MCU. Hardware WP on an F4/H7 board still needs
separate validation; the N6570 can only validate the unsupported-device response.
