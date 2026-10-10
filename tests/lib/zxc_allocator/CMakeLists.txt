cmake_minimum_required(VERSION 3.20)
project(zxc_shared_allocator_test C)
add_executable(zxc_shared_allocator_test main.c shared_heap_stub.c ../../../app/src/scanner_zxc.c)
target_include_directories(zxc_shared_allocator_test PRIVATE include ../../../../modules/lib/zxc/include)
target_compile_definitions(zxc_shared_allocator_test PRIVATE
  CONFIG_ZXC_USE_HEAP=1 CONFIG_ZXC_ALLOCATOR_CUSTOM=1
  CONFIG_VIDEO_BUFFER_USE_SHARED_MULTI_HEAP=1 CONFIG_VIDEO_BUFFER_SMH_ATTRIBUTE=2)
target_compile_options(zxc_shared_allocator_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
enable_testing()
add_test(NAME zxc_shared_allocator COMMAND zxc_shared_allocator_test)
