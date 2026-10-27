# luna-appmanager test and hardening harness

Three layers, smallest first.

## Unit tests (`tests/unit/`)

Built from the component's own sources (everything but `Src/Main.cpp`), with
`unit/fake_ls2.cpp` standing in for luna-service2, so they run without a bus.
The fake records every call and lets a test answer it, or play a service
coming up or going away, the way the bus would.

`test_bootmanager.cpp` drives `BootManager` through first use: it enters
first use once the compositor and WebAppMgr are up, subscribes to SAM's app
life events, launches first use or the phone app again when it stops, leaves
that to the compositor coming back while the compositor is gone, gives up
after five launches in a minute and counts again after the minute or after
the compositor came back, ignores other events, apps and malformed payloads,
and subscribes again when SAM comes back. `BootManager` is a singleton, so
the cases build on each other in the order they are written. On a device
that has finished first use they report that and do nothing.

Build them with the component:

    cmake -DBUILD_TESTING=ON ...
    make luna-appmanager-tests

The runner in `unit/check.h` is the one notificationmgr uses: gtest is not in
the LuneOS sysroot. Run the binary with no arguments for everything, or with
a suite name to filter.

On the build host the corei7-64 build runs against its recipe sysroot:

    <sysroot>/usr/lib/ld-linux-x86-64.so.2 --library-path <sysroot>/usr/lib \
        build/tests/luna-appmanager-tests

## Hardening (`tests/hardening/`)

`run-valgrind.sh` runs that same x86-64 test binary under valgrind and fails
on any memory error or definite leak. `-DENABLE_SANITIZERS=address,undefined`
builds the daemon and the tests with sanitizers, for toolchains that carry
their runtimes.

## On the device (`tests/test-luna-appmanager.sh`)

Talks to the running service with luna-send: functional checks, hostile
payloads and a soak loop. See the script's header.
