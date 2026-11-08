// A crash leaves a trace: on a fatal exception (access violation, stack overflow, abort) the fault, the module base and the raw return addresses are
// written to stderr and to crash.log in the temp folder, so `addr2line -e xl_app.exe -a <addr - base + 0x140000000>` finds the line in a -g build.
#pragma once
void installCrashLog();
