# About Preload Checkers

These started out of the need to monitor some library calls, and catch
violations (called with the wrong state) under Xenomai.

-   try to limit use of system headers, the prototypes of the functions differ
    slightly between c libraries.

-   only depend on the minimum of DSO's, every dependency will be initialized
    before the checker. That means the DSO could call functions that are
    interposed, and this might require workarounds.
    The only thing that is needed is the function `dlsym` from`libdl`,
    which in turn will pull in `libc`.

-   in case the checker interposed a function that does not exists in
    the loaded symbol space, then an access will trap.
    This should not be possible if the application is able to run without the
    checker.

## Intended usage

As these are intended for Xenomai, they will search for a function
`cobalt_assert_nrt`. Maybe I will make this configurable by Macro or
Environment variable. The checkers are usable in any context.

Since this function needs to be a dynamic symbol, an application cannot provide
it directly (well, maybe it does if compiled as `pie`).

On Xenomai, the DSO `libcobalt` provides this function, and it sends a signal
in case it is called from a realtime thread (which should never call the
interposed functions).

However, a testprogram with an accompanying DSO providing that function
demonstrates the usage.
Running this program without a preloaded checker will not find any faults,
after adding the checkers it does.

```bash
# build libraries in CWD
sh PATH_TO/preload_checkers/build.sh
# need to find the DSO's
export LD_LIBRARY_PATH=$(pwd)
# run without checkers
./testpchecker
# should count accesses to heap functions
LD_PRELOAD=./libpchecker_heap.so ./testpchecker
# should count accesses to heap and gettime functions
LD_PRELOAD=./libpchecker_heap.so:./libpchecker_gettime.so ./testpchecker
```

## Making Xenomai (cobalt) stop on errors

The `cobalt_assert_nrt` function will check whether the `PTHREAD_WARNSW`
mode of the currnt thread is enabled. Only if thats the case then
a debug signal will be raised, which will stop a debugger or
kill the process.

To use the checkers, enable the `PTHREAD_WARNSW` mode,
which is supposed to catch unwanted mode-switches.

```c
pthread_set_mode_np(0, PTHREAD_WARNSW);
```

# The checkers themselves

## gettime checker

This interposes the `clock_gettime`, `gettimeofday` and `time` functions
from `libc` (might be `librt` for `clock_gettime`).

This one is straight forward, as the few required functions are unlikely
to call the interposed functions.
The implementation tries to focus on performance, since those functions
can be called often.

## heap checker

This interposes the `malloc`, `free` and more functions operating on the heap.

Unfortunately here might be some temporary heap necessary, as the `dlsym` call
might allocate memory. Otherwise there would be a recursive call to `dlsym`.

Further complications could arise, when another DSO spawns threads
(like a `lttng-ust` preload DSO does), as the implementation is not thread-safe,
and likely this would be hard to do. Hopefully spawning threads will always
need memory allocation, so the resolving on symbols is done before threads can
run.

Because of there complications, there are 3 checker DSOs.

1.  One generic, having a couple KB of a static heap for getting through the
    allocations from resolving symbols.

2.  One for glibc, as glibc exposes all early needed functions also with a
    `__libc_` prefix. With this, the glibc heap checker is similar and as simple
    as the gettime checker.

3.  One for musl, since musl does not need to allocate memory for resolving
    symbols, and the musl heap checker is similar and as simple
    as the gettime checker.

    If trying to resolve a symbol that *does not exist*, musl will need a
    heap allocation. For that reason the basic c functions are resolved first.

# Debugging with gdb

## Problems starting the target executable

Setting the `LD_PRELOAD` environment variable will not only preload these
DSO's for the target, but also for the debugger itself as well as the shell
that the debugger uses per default to start the given commandline.

Because of there reasons, you need to set the environment variable within
gdb, and tell gdb to not use the shell for staring the program.

```bash
gdb \
    -iex 'set environment LD_PRELOAD=/tmp/libcobalt-malloc_checker.so' \
    -iex 'set startup-with-shell off' /tmp/testcobalt-checker
```

## Debugging problems with startup

The order of startup and the way symbols are prioritized is tricky and not
obvious.
The best way to trace what is happening is using the `LD_DEBUG` variable,
which is probably only supported by glibc.

gdb supports stopping on DSO events, but there seems to be a single one
for the entire startup: `set stop-on-solib-events 1`
