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
and subscribes again, and launches both apps again, when SAM comes back. `BootManager` is a singleton, so
the cases build on each other in the order they are written. On a device
that has finished first use they report that and do nothing.

`test_bootmanager.cpp` then puts `BootManager` in the normal state and
checks that entering it opens what first use's hand-off file asked for (the
Backup app on its restore flow, the Accounts app) and removes the file, that
it launches the boot time apps again once SAM comes back (only
those not running, with `{"launchedAtBoot":true}`, not an app closed on
purpose), launches the KeepAlive phone app again when it stops, waits for
SAM's answer to `running` before launching anything and ignores an answer
that is missing, malformed or from before SAM went, drops its checks when
normal operation ends, and gives up on a phone app that keeps stopping
until SAM comes back.

`test_bootappkeeper.cpp` covers `BootAppKeeper`, which makes those
decisions, on its own: the wait after SAM came back, apps closed on
purpose, KeepAlive apps, the back-off (1, 2, 4, 8, 16 seconds), giving up
after five launches in a minute and what ends that. Every case there has
its own keeper and clock.

`test_firstuse_handoff.cpp` covers `FirstUseHandoff`, which reads that file,
on its own: what each key launches and in what order, that only a JSON
`true` turns one on, that the file cannot name an app or parameters, and
that a malformed, oversized, symlinked, directory or FIFO hand-off launches
nothing, does not block, and is removed (a symlink's target is left alone).

`test_open_ipk.cpp` checks that `com.palm.applicationManager/open` installs
a package only for the trusted installer apps: an app's open of a target
named `.ipk` is refused, and so is the install of a download that only turns
out to be an ipk once it is done (a redirect renames it), while a trusted
installer's download and `com.palm.appinstaller/install`'s are still
installed. The installer runs in minimal mode there, so an install that is
let through is queued and never run.

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
