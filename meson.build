project('ish', 'c',
    default_options: ['default_library=static', 'c_std=gnu11', 'warning_level=2'])
cc = meson.get_compiler('c')

if cc.get_id() == 'clang'
    add_project_arguments('-Wimplicit-fallthrough', '-Wtautological-constant-in-range-compare', language: 'c')
endif

if get_option('b_sanitize').split(',').contains('undefined')
    add_project_arguments('-fno-sanitize=alignment', language: 'c')
endif

log_on = get_option('log').split()
log_off = get_option('nolog').split()
foreach channel : log_on + log_off
    if log_on.contains(channel)
        add_project_arguments('-DDEBUG_' + channel + '=1', language: 'c')
    else
        add_project_arguments('-DDEBUG_' + channel + '=0', language: 'c')
    endif
endforeach
add_project_arguments('-DLOG_HANDLER_' + get_option('log_handler').to_upper() + '=1', language: 'c')

# Architecture: aarch64 only (iOS migration) - TCTI is the only execution engine
add_project_arguments('-DARCH_AARCH64=1', language: 'c')
add_project_arguments('-DENGINE_TCTI=1', language: 'c')

# Enable performance counters for optimization work
if get_option('enable_perf_stats')
    add_project_arguments('-DENABLE_PERF_STATS=1', language: 'c')
endif

# Enable profiling infrastructure
if get_option('enable_profiling')
    add_project_arguments('-DENABLE_PROFILING=1', language: 'c')
endif

if get_option('no_crlf')
    add_project_arguments('-DNO_CRLF', language: 'c')
endif

add_project_arguments('-Wno-switch', language: 'c')

# Architecture detection (aarch64 only for iOS)
arch = 'aarch64'

includes = [include_directories('.'), include_directories('trace')]

threads = dependency('threads')
librt = cc.find_library('rt', required: false)
libm = cc.find_library('m', required: false)
libdl = cc.find_library('dl', required: false)
sqlite3 = cc.find_library('sqlite3')
dependencies = [librt, libm, libdl, threads, sqlite3]

subdir('vdso') # ish depends on the vdso

# TCTI Gadget Generation
# Generated files are build artifacts, not source-of-truth
# Source of truth: tcti/aarch64/tcti-gadget-gen.py
# Generated outputs: build directory (disposable)
tcti_gadget_gen = custom_target('tcti_gadgets',
    output: ['gadgets_tcti.h', 'gadgets_tcti_impl.c'],
    input: 'tcti/aarch64/tcti-gadget-gen.py',
    command: [find_program('python3'), '@INPUT@', '-o', '@OUTDIR@', '--generator-hash', meson.project_source_root() / 'tcti/aarch64/tcti-gadget-gen.py'],
    build_by_default: true,
    install: false,
)

# aarch64 CPU offsets (for TCTI register mapping)
aarch64_offsets = custom_target('aarch64_offsets',
    output: 'aarch64-cpu-offsets.h',
    input: 'tcti/aarch64/offsets.c',
    depfile: 'aarch64-cpu-offsets.h.d',
    command: [find_program('tools/staticdefine.sh'), '@OUTDIR@/compile_commands.json', '@INPUT@', '@OUTPUT@', '@DEPFILE@'],
)

emu_src = [
    # Core emulator
    'emu/tlb.c',
    'emu/aarch64/cpu.c',
    'emu/aarch64/decode.c',
    'emu/aarch64/memory.c',
    'emu/aarch64/tls.c',
    'emu/aarch64/block-cache.c',

    # TCTI code generator (aarch64-specific)
    'tcti/aarch64/gen.c',
    'tcti/aarch64/gadgets_arith.c',
    'tcti/aarch64/gadgets_math.c',
    'tcti/aarch64/gadgets_memory.c',
    
    # TCTI execution context management
    'tcti/frame.c',
    
    # TCTI entry/exit assembly
    'tcti/aarch64/tcti_entry.S',

    # Tracing subsystem (always built)
    'trace/trace.c',
    'trace/trace_backends.c',
    'trace/trace_ring.c',
    'trace/trace_dump.c',
    'trace/trace_config.c',

    aarch64_offsets,
    tcti_gadget_gen,
]

libish_emu = library('ish_emu', emu_src, include_directories: includes)

libfakefs = library('fakefs',
    ['fs/fake-db.c', 'fs/fake-migrate.c', 'fs/fake-rebuild.c'],
    include_directories: includes,
    dependencies: sqlite3)

subdir('deps')

# TCTI is the only supported execution engine for AArch64
src = [
        'kernel/init.c',
        'kernel/errno.c',

        'kernel/aarch64/calls.c',
        'kernel/aarch64/syscall_dispatch.c',
        'kernel/aarch64/signal.c',
        'kernel/aarch64/vdso.c',
        'kernel/memory.c',
        'kernel/user.c',
        'kernel/vdso.c', vdso,
        'kernel/task.c',
        'kernel/group.c',
        'kernel/log.c',

        'kernel/fork.c',
        'kernel/exec.c',
        'kernel/exit.c',
        'kernel/time.c',
        'kernel/mmap.c',
        'kernel/uname.c',
        'kernel/tls.c',
        'kernel/futex.c',
        'kernel/getset.c',
        'kernel/signal.c',
        'kernel/resource.c',
        'kernel/random.c',
        'kernel/misc.c',
        'kernel/eventfd.c',
        'kernel/ipc.c',
        'kernel/ptrace.c',

        'kernel/fs.c',
        'kernel/fs_info.c',
        'fs/mount.c',
        'fs/fd.c',
        'fs/inode.c',
        'fs/stat.c',
        'fs/dir.c',
        'fs/generic.c',
        'fs/path.c',
        'fs/real.c',
        'fs/fake.c',

        'fs/proc.c',
        'fs/proc/entry.c',
        'fs/proc/ish.c',
        'fs/proc/root.c',
        'fs/proc/pid.c',

        'fs/dyndev.c',

        'fs/adhoc.c',
        'fs/sock.c',
        'fs/pipe.c',
        'fs/sockrestart.c',
        'fs/lock.c',

        'fs/dev.c',
        'fs/mem.c',
        'fs/tty.c',
        'fs/tty-real.c',
        'fs/pty.c',
        'fs/tmp.c',

        'fs/poll.c',
        'kernel/poll.c',
        'kernel/epoll.c',

        'util/timer.c',
        'util/sync.c',
        'util/fifo.c',
        'util/fchdir.c',

        # Profiling infrastructure (compile always, zero-cost when disabled)
        'util/prof.c',

        'platform/' + host_machine.system() + '.c',
    ]

    libish = library('ish', src,
        include_directories: includes)
    ish = declare_dependency(
        link_with: [libish, libish_emu, libfakefs],
        dependencies: dependencies,
        include_directories: includes)

if not meson.is_cross_build()
    executable('ish', ['main.c'], dependencies: ish)
endif

subdir('tools')

gdb_scripts = ['ish-gdb.gdb']
foreach script : gdb_scripts
    custom_target(script,
        output: script, input: script,
        command: ['ln', '-sf', '@INPUT@', '@OUTPUT@'],
        build_by_default: true)
endforeach

# Validate generated files are not in source tree
validate_generated = run_target('validate-generated',
    command: [find_program('tools/validate_generated.sh')]
)

# Profiling targets (require -Denable_profiling=true)
if get_option('enable_profiling')
    # Profile analysis tool
    profile_tool = find_program('tools/ish-profile-tool.py', required: false)
    
    # Run target: profile-run - run ish with profiling enabled
    profile_run = run_target('profile-run',
        command: [find_program('scripts/profile-run.sh')],
        depends: ish
    )
    
    # Run target: profile-analyze - analyze profile output
    if profile_tool.found()
        run_target('profile-analyze',
            command: [profile_tool, 'analyze', 'ish-profile.json']
        )
        
        run_target('profile-flame',
            command: [profile_tool, 'flamegraph', 'ish-profile.json']
        )
    endif
endif

# Case system (bootstrap - 6 cases)
subdir('tests/cases')

# === Development Run Targets ===

# Lint targets
run_target('lint-format',
    command: [find_program('tools/lint-format.sh')]
)

run_target('lint-cppcheck',
    command: [find_program('tools/lint-cppcheck.sh')]
)

run_target('lint-tidy',
    command: [find_program('tools/lint-tidy.sh')]
)

run_target('lint',
    command: [find_program('tools/lint-all.sh')]
)

# Format target
run_target('format',
    command: [find_program('tools/format-code.sh')]
)

# Utility targets
run_target('info',
    command: [find_program('tools/project-info.sh')]
)

run_target('loc',
    command: [find_program('tools/count-loc.sh')]
)

run_target('todos',
    command: [find_program('tools/find-todos.sh')]
)

# Dead code check
run_target('dead-code',
    command: [find_program('tools/dead-code-check.py'), '--exit-code', 'emu/', 'kernel/', 'fs/', 'tcti/', 'util/']
)

run_target('check-all',
    command: [find_program('tools/check-all.sh')]
)

# Pre-commit (optional - only if installed)
pre_commit = find_program('pre-commit', required: false)
if pre_commit.found()
    run_target('pre-commit',
        command: [pre_commit, 'run', '--all-files']
    )
endif
