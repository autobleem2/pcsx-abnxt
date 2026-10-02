# pcsx-abnxt: the AutoBleem build of PCSX-ReARMed.
#
# Upstream builds with ./configure + Makefile (+ a Makefile per plugin); those stay in the tree untouched, as a
# cheap upstream-parity check on Linux, and this file is what AutoBleem builds with. Every choice configure
# guesses is an explicit option below, set by the toolchain file for the target in question:
#
#   toolchains/psc/PSCtoolchainV8.cmake   PlayStation Classic (armv8-a 32-bit, gcc-6)  -> ci/build.sh psc
#   toolchains/rpi/RPitoolchain.cmake     Raspberry Pi, 32-bit OS (armv7-a + NEON)      -> ./make_rpi.sh
#   toolchains/rpi64/RPi64toolchain.cmake Raspberry Pi, 64-bit OS (aarch64)             -> ./make_rpi64.sh
#   (none, MSYS2 UCRT64)                  Windows development build                     -> ./make_win.sh
#   (none)                                a native Linux build (the CI's smoke run)
#
# Targets: the emulator executable (build dir root; its file name stays "pcsx-ab" because that is what
# AutoBleem's launch scripts run) and the loadable GPU/SPU plugins in plugins/, the layout of
# Autobleem/bin/emu/.

cmake_minimum_required(VERSION 3.16)
project(pcsx-abnxt LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)          # gnu11: the dynarecs, libchdr and libpicofe use GNU extensions freely
set(CMAKE_CXX_STANDARD 11)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# Upstream's Makefile builds with -Ofast (CFLAGS_OPT); the dynarec gets -O2 "for less bloat" (below).
foreach(_lang C CXX)
    if (CMAKE_${_lang}_FLAGS_RELEASE STREQUAL "-O3 -DNDEBUG")
        set(CMAKE_${_lang}_FLAGS_RELEASE "-Ofast -DNDEBUG")
    endif()
endforeach()

# ---------------------------------------------------------------------------------------------------------
# options - the knobs configure guesses
# ---------------------------------------------------------------------------------------------------------
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|ARM)")
    set(_pcsxab_arch arm)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)")
    set(_pcsxab_arch aarch64)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)")
    set(_pcsxab_arch x86_64)
else()
    set(_pcsxab_arch other)
endif()
if (_pcsxab_arch STREQUAL "arm")
    set(_pcsxab_neon_default ON)
else()
    set(_pcsxab_neon_default OFF)
endif()

set(PCSXAB_PLATFORM "sdl2" CACHE STRING "Frontend platform: sdl2 (ours: frontend/plat_autobleem.c - GL video, SDL2 input - every target) or headless (no video/input - a link check)")
set_property(CACHE PCSXAB_PLATFORM PROPERTY STRINGS sdl2 headless)
set(PCSXAB_DYNAREC "auto" CACHE STRING "CPU recompiler: auto, ari64 (ARM/AArch64), lightrec (everything else), none (interpreter)")
set_property(CACHE PCSXAB_DYNAREC PROPERTY STRINGS auto ari64 lightrec none)
option(PCSXAB_NEON        "ARM NEON assembly (GTE, GPU, scalers). 32-bit ARMv7+ only; AArch64 uses the C SIMD code" ${_pcsxab_neon_default})
option(PCSXAB_THREADS     "The multithreaded features: asynchronous CD-ROM, GPU and SPU, and the dynarec's compile thread" ON)
option(PCSXAB_PLUGINS     "Also build the loadable plugins (gpu_peops, gpu_unai [, gpu_neon, gpu_gles], spunull)" ON)
option(PCSXAB_GLES        "OpenGL ES output in the SDL frontend (libpicofe's gl.c, EGL loaded at run time)" OFF)
option(PCSXAB_LINK_MAP    "Write pcsx-ab.map next to the executable" ON)
option(PCSXAB_CHD         "CHD disc images (deps/libchdr, static, with its own lzma and zstd; zlib is the system one)" ON)
set(PCSXAB_BUILTIN_GPU "neon" CACHE STRING "GPU compiled into the executable: neon (NEON assembly on 32-bit ARM, C SIMD elsewhere), peops or unai")
set_property(CACHE PCSXAB_BUILTIN_GPU PROPERTY STRINGS neon peops unai)
set(PCSXAB_SOUND_DRIVERS "sdl" CACHE STRING "Space-separated SPU output drivers: sdl alsa oss pulseaudio")

if (PCSXAB_DYNAREC STREQUAL "auto")
    if (_pcsxab_arch STREQUAL "arm" OR _pcsxab_arch STREQUAL "aarch64")
        set(_pcsxab_dynarec ari64)
    else()
        set(_pcsxab_dynarec lightrec)
    endif()
else()
    set(_pcsxab_dynarec ${PCSXAB_DYNAREC})
endif()
if (_pcsxab_dynarec STREQUAL "ari64" AND NOT (_pcsxab_arch STREQUAL "arm" OR _pcsxab_arch STREQUAL "aarch64"))
    message(FATAL_ERROR "the ari64 dynarec is ARM/AArch64 only (CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})")
endif()
if (PCSXAB_NEON AND NOT _pcsxab_arch STREQUAL "arm")
    message(FATAL_ERROR "PCSXAB_NEON is the 32-bit ARM NEON assembly; AArch64 and x86 use the C SIMD code without it")
endif()
separate_arguments(_pcsxab_sound_drivers UNIX_COMMAND "${PCSXAB_SOUND_DRIVERS}")

# ---------------------------------------------------------------------------------------------------------
# dependencies
# ---------------------------------------------------------------------------------------------------------
# Native builds: the distribution's -dev packages. Cross builds: the toolchain file's CMAKE_MODULE_PATH
# supplies FindSDL*/FindPNG for a sysroot without them.
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
if (PCSXAB_PLATFORM STREQUAL "sdl2")
    # sdl2-config.cmake on a host, the toolchain dir's FindSDL2.cmake on a cross sysroot without it
    find_package(SDL2 REQUIRED)
    if (NOT TARGET SDL2::SDL2)
        # sdl2-config.cmake older than 2.0.12 only sets the variables
        add_library(SDL2::SDL2 INTERFACE IMPORTED)
        set_target_properties(SDL2::SDL2 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}")
    endif()
    # the sources include <SDL.h>: make sure the SDL2 directory itself is on the path, whichever of it
    # or its parent the package names
    get_target_property(_pcsxab_sdl_inc SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
    set(_pcsxab_sdl_incs "")
    foreach(_dir ${_pcsxab_sdl_inc})
        list(APPEND _pcsxab_sdl_incs "${_dir}")
        get_filename_component(_name "${_dir}" NAME)
        if (NOT _name STREQUAL "SDL2" AND EXISTS "${_dir}/SDL2")
            list(APPEND _pcsxab_sdl_incs "${_dir}/SDL2")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _pcsxab_sdl_incs)
    set(_pcsxab_sdl_libs SDL2::SDL2)
endif()

# ---------------------------------------------------------------------------------------------------------
# flags common to the executable and the plugins (the Makefile's CFLAGS + what configure appends)
# ---------------------------------------------------------------------------------------------------------
add_compile_options(-Wall -ffast-math -ffunction-sections -fdata-sections -Wno-unused-result)
add_compile_definitions(_REENTRANT _FILE_OFFSET_BITS=64 DISABLE_MEM_LUTS=0)
# AutoBleem's memory-card sets are one card, swapped in as card1.mcd; card2 stays for a second player's
add_compile_definitions(PCSX_MEMCARD_COUNT=2)
# our hooks in upstream's files are gated the way its ports are (-DPANDORA, -DMAEMO): PSCLASSIC is the
# platform name upstream's configure/Makefile know us by (./configure --platform=psclassic builds the
# same emulator); without it the tree is upstream's
add_compile_definitions(PSCLASSIC)
if (WIN32)
    # deps/mman is mmap() for MinGW; the CRT has no posix_memalign
    # NO_DYLIB is upstream's own Windows recipe (Makefile.libretro): the plugins are built in and
    # SysLibError() answers NULL - which libpcsxcore/plugins.c's CheckErr needs, anything else is a failure
    add_compile_definitions(P_HAVE_MMAP=1 P_HAVE_POSIX_MEMALIGN=0 __WIN32__ NO_DYLIB)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/mman ${CMAKE_CURRENT_SOURCE_DIR}/deps/mman)
else()
    add_compile_definitions(P_HAVE_MMAP=1 P_HAVE_POSIX_MEMALIGN=1)
endif()
if (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 10)
    # GCC 10 switched to -fno-common; the tree still has tentative definitions in headers
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fcommon> $<$<COMPILE_LANGUAGE:CXX>:-fcommon>)
endif()
if (_pcsxab_arch STREQUAL "arm")
    # the recompiler cannot handle Thumb code
    add_compile_options(-marm)
endif()
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    add_link_options(-Wl,--gc-sections)
endif()
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# include/revision.h: the Makefile regenerates it from `git describe` on every build; here at configure
# time into the build tree (re-run cmake to pick up a new tag). A tree without git history (the build
# server's rsync copy) takes AB_GIT_DESCRIBE from the environment - what the caller's `git describe` said,
# passed into the Docker image like AutoBleem's AB_GIT_* - and REV "" without that.
find_package(Git QUIET)
set(_pcsxab_rev "")
if (GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _pcsxab_rev OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    # -dirty by our own files only: the submodules are upstream's and never edited here, and on Windows
    # MSYS2's git and Git for Windows disagree about their line endings, which made every build "dirty"
    execute_process(COMMAND ${GIT_EXECUTABLE} update-index -q --refresh --ignore-submodules
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} ERROR_QUIET OUTPUT_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} diff-index --quiet --ignore-submodules=all HEAD --
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE _pcsxab_dirty ERROR_QUIET)
    if (NOT _pcsxab_rev STREQUAL "" AND NOT _pcsxab_dirty EQUAL 0)
        set(_pcsxab_rev "${_pcsxab_rev}-dirty")
    endif()
endif()
if (_pcsxab_rev STREQUAL "" AND DEFINED ENV{AB_GIT_DESCRIBE})
    set(_pcsxab_rev "$ENV{AB_GIT_DESCRIBE}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/revision.h.in" "#define REV \"${_pcsxab_rev}\"\n")
configure_file("${CMAKE_CURRENT_BINARY_DIR}/revision.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/include/revision.h" COPYONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/generated/include)

# ---------------------------------------------------------------------------------------------------------
# libchdr (deps/libchdr): the object list upstream's Makefile compiles into the binary, as a static library
# ---------------------------------------------------------------------------------------------------------
if (PCSXAB_CHD)
    set(_chdr deps/libchdr)
    set(_lzma ${_chdr}/deps/lzma-24.05)
    set(_zstd ${_chdr}/deps/zstd-1.5.6/lib)
    add_library(chdr STATIC
        ${_chdr}/src/libchdr_bitstream.c ${_chdr}/src/libchdr_cdrom.c ${_chdr}/src/libchdr_chd.c
        ${_chdr}/src/libchdr_flac.c ${_chdr}/src/libchdr_huffman.c
        ${_lzma}/src/Alloc.c ${_lzma}/src/CpuArch.c ${_lzma}/src/Delta.c ${_lzma}/src/LzFind.c
        ${_lzma}/src/LzmaDec.c ${_lzma}/src/LzmaEnc.c ${_lzma}/src/Sort.c
        ${_zstd}/common/entropy_common.c ${_zstd}/common/error_private.c ${_zstd}/common/fse_decompress.c
        ${_zstd}/common/xxhash.c ${_zstd}/common/zstd_common.c
        ${_zstd}/decompress/huf_decompress.c ${_zstd}/decompress/huf_decompress_amd64.S
        ${_zstd}/decompress/zstd_ddict.c ${_zstd}/decompress/zstd_decompress_block.c
        ${_zstd}/decompress/zstd_decompress.c)
    # a directory include, not the target's: deps/libretro-common/include carries libchdr headers of its own
    # (the VFS-flavoured ones), and this one has to come before it in the search order
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/${_chdr}/include)
    target_include_directories(chdr PRIVATE ${_lzma}/include ${_zstd})
    target_compile_definitions(chdr PRIVATE Z7_ST)
    target_compile_options(chdr PRIVATE -w)     # vendored: not made warning-free
    target_link_libraries(chdr PUBLIC ZLIB::ZLIB)
    set_target_properties(chdr PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# ---------------------------------------------------------------------------------------------------------
# the executable
# ---------------------------------------------------------------------------------------------------------
set(PCSXAB_SOURCES
    # core
    libpcsxcore/cdriso.c libpcsxcore/cdrom.c libpcsxcore/cdrom-async.c libpcsxcore/cheat.c
    libpcsxcore/database.c libpcsxcore/decode_xa.c libpcsxcore/mdec.c libpcsxcore/misc.c
    libpcsxcore/plugins.c libpcsxcore/ppf.c libpcsxcore/psxbios.c libpcsxcore/psxcommon.c
    libpcsxcore/psxcounters.c libpcsxcore/psxdma.c libpcsxcore/psxhw.c libpcsxcore/psxinterpreter.c
    libpcsxcore/psxmem.c libpcsxcore/psxevents.c libpcsxcore/r3000a.c libpcsxcore/sio.c
    libpcsxcore/spu.c libpcsxcore/gpu.c libpcsxcore/pad.c
    libpcsxcore/state_sony.c
    libpcsxcore/gte.c libpcsxcore/gte_divider.c
    libpcsxcore/new_dynarec/emu_if.c
    # spu (dfsound is always built in)
    plugins/dfsound/dma.c plugins/dfsound/freeze.c plugins/dfsound/registers.c plugins/dfsound/spu.c
    plugins/dfsound/out.c plugins/dfsound/nullsnd.c
    # gpulib glue, non-PIC copy for the built-in gpu (the plugins get their own PIC build below)
    plugins/gpulib/gpu.c plugins/gpulib/vout_pl.c plugins/gpulib/prim.c
    # frontend
    frontend/cspace.c frontend/plugin_lib.c frontend/plugin.c frontend/main.c frontend/menu.c
    frontend/libpicofe/input.c frontend/libpicofe/readpng.c frontend/libpicofe/fonts.c
    # AutoBleem's own (the port's rule: new files, hooks only in upstream's)
    frontend/ab/ab_config.c frontend/ab/ab_session.c frontend/ab/ab_buttons.c frontend/ab/ab_memcard.c
    frontend/ab/ab_console.c frontend/ab/ab_disc.c frontend/ab/ab_ui.c frontend/ab/ab_hacks.c
    frontend/ab/ab_pad_battery.c frontend/ab/ab_headless.c frontend/ab/ab_hud_text.c
    # the smoothing scalers: ours (scale2x/eagle2x in C) and hqx (LGPL, adapted to RGB565)
    frontend/ab/ab_scaler.c frontend/ab/hqx/hqx_init.c frontend/ab/hqx/hq2x.c frontend/ab/hqx/hq3x.c
)
# the hqx sources are vendored code with their own style
set_source_files_properties(frontend/ab/hqx/hq2x.c frontend/ab/hqx/hq3x.c PROPERTIES COMPILE_OPTIONS "-O3;-w")
set_source_files_properties(frontend/libpicofe/input.c PROPERTIES COMPILE_OPTIONS -Wno-array-bounds)
# stb_truetype (STBTT_STATIC) defines its whole API; we use a corner of it
set_source_files_properties(frontend/ab/ab_ui.c PROPERTIES COMPILE_OPTIONS -Wno-unused-function)
set_source_files_properties(libpcsxcore/psxbios.c PROPERTIES COMPILE_OPTIONS -Wno-nonnull)
set_source_files_properties(libpcsxcore/cdriso.c PROPERTIES COMPILE_OPTIONS -Wno-unused-function)
set_source_files_properties(frontend/menu.c PROPERTIES COMPILE_DEFINITIONS "SDL_OVERLAY_2X;MENU_SHOW_VARSCALER=1")

# the host layer: libpicofe's linux/ on the targets, frontend/win32/ for the MinGW development build
if (WIN32)
    list(APPEND PCSXAB_SOURCES frontend/win32/plat_win32.c deps/mman/mman.c)
    # our <dirent.h> (with d_type/scandir) must shadow mingw's: -I dirs are searched before system ones;
    # win32_compat.h is force-included into every C file (mkdir's mode argument, SIGPIPE, <sys/stat.h>)
    # so that no upstream source needs a Windows #ifdef
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/frontend/win32/include)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/frontend/win32/include/win32_compat.h>)
else()
    list(APPEND PCSXAB_SOURCES frontend/libpicofe/linux/plat.c)
    set_source_files_properties(frontend/libpicofe/linux/plat.c PROPERTIES COMPILE_DEFINITIONS NO_HOME_DIR)
endif()

# the platform
if (PCSXAB_PLATFORM STREQUAL "sdl2")
    # libpicofe's plat_dummy.c is the (empty) plat_target, as for upstream's generic platform
    # our GL video (plat_autobleem, docs/render-pipeline-plan.md) + SDL2 input; ab_shaders.c = its passes
    list(APPEND PCSXAB_SOURCES frontend/plat_autobleem.c frontend/libpicofe/plat_autobleem.c
        frontend/ab/ab_shaders.c frontend/ab/ab_debug.c
        frontend/libpicofe/in_sdl2.c frontend/libpicofe/in_sdl2gc.c frontend/libpicofe/plat_dummy.c)
elseif (PCSXAB_PLATFORM STREQUAL "headless")
    list(APPEND PCSXAB_SOURCES frontend/plat_dummy.c frontend/libpicofe/plat_dummy.c)
else()
    message(FATAL_ERROR "unknown PCSXAB_PLATFORM '${PCSXAB_PLATFORM}'")
endif()

# gte_nf.o is gte.c compiled a second time with -DFLAGLESS (the flag-less GTE variant the dynarec calls
# into). CMake will not compile one source twice in one target, so a one-line wrapper does it.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gte_nf.c.in"
    "#define FLAGLESS\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/libpcsxcore/gte.c\"\n")
configure_file("${CMAKE_CURRENT_BINARY_DIR}/gte_nf.c.in" "${CMAKE_CURRENT_BINARY_DIR}/gte_nf.c" COPYONLY)
list(APPEND PCSXAB_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/gte_nf.c")

# architecture code
if (_pcsxab_arch STREQUAL "arm")
    list(APPEND PCSXAB_SOURCES libpcsxcore/gte_arm.S plugins/dfsound/arm_utils.S)
endif()
if (PCSXAB_NEON)
    list(APPEND PCSXAB_SOURCES libpcsxcore/gte_neon.S frontend/cspace_neon.S
        frontend/libpicofe/arm/neon_scale2x.S frontend/libpicofe/arm/neon_eagle2x.S)
    set_source_files_properties(frontend/libpicofe/arm/neon_scale2x.S frontend/libpicofe/arm/neon_eagle2x.S
        PROPERTIES COMPILE_DEFINITIONS DO_BGR_TO_RGB)
    set_source_files_properties(frontend/cspace.c PROPERTIES COMPILE_DEFINITIONS "HAVE_bgr555_to_rgb565;HAVE_bgr888_to_x")
elseif (_pcsxab_arch STREQUAL "arm")
    list(APPEND PCSXAB_SOURCES frontend/cspace_arm.S)
    set_source_files_properties(frontend/cspace.c PROPERTIES COMPILE_DEFINITIONS HAVE_bgr555_to_rgb565)
endif()

# the dynarec
set(_pcsxab_thread_defs "")
if (_pcsxab_dynarec STREQUAL "ari64")
    list(APPEND PCSXAB_SOURCES libpcsxcore/new_dynarec/new_dynarec.c libpcsxcore/new_dynarec/pcsxmem.c)
    set_source_files_properties(libpcsxcore/new_dynarec/new_dynarec.c PROPERTIES COMPILE_OPTIONS -O2)  # less bloat
    if (_pcsxab_arch STREQUAL "arm")
        list(APPEND PCSXAB_SOURCES libpcsxcore/new_dynarec/linkage_arm.S)
    else()
        list(APPEND PCSXAB_SOURCES libpcsxcore/new_dynarec/linkage_arm64.S)
    endif()
    if (PCSXAB_THREADS)
        set_property(SOURCE libpcsxcore/new_dynarec/new_dynarec.c libpcsxcore/new_dynarec/emu_if.c
            APPEND PROPERTY COMPILE_DEFINITIONS NDRC_THREAD)
    endif()
elseif (_pcsxab_dynarec STREQUAL "lightrec")
    set(_lr deps/lightrec)
    set(_ln deps/lightning)
    list(APPEND PCSXAB_SOURCES libpcsxcore/lightrec/plugin.c
        ${_lr}/tlsf/tlsf.c ${_lr}/blockcache.c ${_lr}/constprop.c ${_lr}/disassembler.c ${_lr}/emitter.c
        ${_lr}/interpreter.c ${_lr}/lightrec.c ${_lr}/memmanager.c ${_lr}/optimizer.c ${_lr}/regcache.c
        ${_ln}/lib/jit_disasm.c ${_ln}/lib/jit_memory.c ${_ln}/lib/jit_names.c ${_ln}/lib/jit_note.c
        ${_ln}/lib/jit_print.c ${_ln}/lib/jit_size.c ${_ln}/lib/lightning.c)
    include_directories(${_ln}/include ${_lr} ${CMAKE_CURRENT_SOURCE_DIR}/include/lightning
        ${CMAKE_CURRENT_SOURCE_DIR}/include/lightrec)
    add_compile_definitions(LIGHTREC LIGHTREC_STATIC LIGHTREC_CUSTOM_MAP=0 LIGHTREC_CODE_INV=0
        LIGHTREC_ENABLE_THREADED_COMPILER=0 LIGHTREC_ENABLE_DISASSEMBLER=0 LIGHTREC_NO_DEBUG=1)
    file(GLOB _pcsxab_lr_sources ${_lr}/*.c ${_lr}/tlsf/*.c ${_ln}/lib/*.c)
    set_source_files_properties(${_pcsxab_lr_sources} PROPERTIES COMPILE_OPTIONS "-Wno-unused;-Wno-unused-function;-Wno-uninitialized")
    set_property(SOURCE ${_ln}/lib/lightning.c APPEND PROPERTY COMPILE_DEFINITIONS HAVE_MMAP=P_HAVE_MMAP)
    if (WIN32)
        set_property(SOURCE ${_ln}/lib/lightning.c APPEND PROPERTY COMPILE_DEFINITIONS mprotect=_mprotect)
        set_property(SOURCE ${_ln}/lib/jit_print.c APPEND PROPERTY COMPILE_OPTIONS -w)
    endif()
else()
    add_compile_definitions(DRC_DISABLE)
endif()

# the thread library is always in (menu.c shows the core count it reports); the multithreaded features
# themselves (configure's --enable-threads, on for the generic platform) are the option
list(APPEND PCSXAB_SOURCES frontend/pcsxr-threads.c deps/libretro-common/features/features_cpu.c)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/libretro-common/include)
set_source_files_properties(deps/libretro-common/features/features_cpu.c PROPERTIES COMPILE_OPTIONS -w)
if (PCSXAB_THREADS)
    list(APPEND PCSXAB_SOURCES plugins/gpulib/gpu_async.c)
    # the Makefile sets these per object (cdrom-async, gpulib, dfsound, menu); on the whole executable they
    # reach the same files and nothing else looks at them. Not on the source files: gpulib's are shared
    # with the plugins' gpulib_pic, which upstream builds without the async code.
    set(_pcsxab_thread_defs HAVE_RTHREADS USE_ASYNC_CDROM USE_ASYNC_GPU USE_ASYNC_SPU)
endif()

# built-in gpu
if (PCSXAB_BUILTIN_GPU STREQUAL "neon")
    list(APPEND PCSXAB_SOURCES plugins/gpu_neon/psx_gpu_if.c)
    add_compile_definitions(GPU_NEON)
    set_property(SOURCE frontend/menu.c frontend/plugin_lib.c APPEND PROPERTY COMPILE_DEFINITIONS BUILTIN_GPU_NEON)
    if (PCSXAB_NEON)
        list(APPEND PCSXAB_SOURCES plugins/gpu_neon/psx_gpu/psx_gpu_arm_neon.S)
        set_source_files_properties(plugins/gpu_neon/psx_gpu_if.c PROPERTIES
            COMPILE_DEFINITIONS "NEON_BUILD;TEXTURE_CACHE_4BPP;TEXTURE_CACHE_8BPP")
    else()
        list(APPEND PCSXAB_SOURCES plugins/gpu_neon/psx_gpu/psx_gpu_simd.c)
        set_source_files_properties(plugins/gpu_neon/psx_gpu_if.c plugins/gpu_neon/psx_gpu/psx_gpu_simd.c PROPERTIES
            COMPILE_DEFINITIONS "NEON_BUILD;TEXTURE_CACHE_4BPP;TEXTURE_CACHE_8BPP;SIMD_BUILD")
        # the C SIMD code is SSE2 on x86: a 32-bit build at Debian's i686 baseline (AutoBleem's PC stick)
        # gets SSE2 for these two files alone - the GPU then wants a Pentium 4 / Athlon 64 or later, the
        # rest of the emulator stays i686
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "^i[3-6]86$")
            set_property(SOURCE plugins/gpu_neon/psx_gpu_if.c plugins/gpu_neon/psx_gpu/psx_gpu_simd.c
                APPEND PROPERTY COMPILE_OPTIONS -msse2)
        endif()
    endif()
elseif (PCSXAB_BUILTIN_GPU STREQUAL "peops")
    list(APPEND PCSXAB_SOURCES plugins/dfxvideo/gpulib_if.c)
    add_compile_definitions(GPU_PEOPS)
    set_property(SOURCE frontend/menu.c frontend/plugin_lib.c APPEND PROPERTY COMPILE_DEFINITIONS BUILTIN_GPU_PEOPS)
    # note: code is not safe for strict-aliasing? (Castlevania problems)
    set_source_files_properties(plugins/dfxvideo/gpulib_if.c PROPERTIES COMPILE_OPTIONS -fno-strict-aliasing)
elseif (PCSXAB_BUILTIN_GPU STREQUAL "unai")
    list(APPEND PCSXAB_SOURCES plugins/gpu_unai/gpulib_if.cpp plugins/gpu_unai/old/if.cpp)
    add_compile_definitions(GPU_UNAI USE_GPULIB=1)
    set_property(SOURCE frontend/menu.c frontend/plugin_lib.c APPEND PROPERTY COMPILE_DEFINITIONS BUILTIN_GPU_UNAI)
    set_source_files_properties(plugins/gpu_unai/gpulib_if.cpp PROPERTIES COMPILE_DEFINITIONS REARMED COMPILE_OPTIONS -O3)
    set_source_files_properties(plugins/gpu_unai/old/if.cpp PROPERTIES COMPILE_OPTIONS -O3)
    if (_pcsxab_arch STREQUAL "arm")
        list(APPEND PCSXAB_SOURCES plugins/gpu_unai/gpu_arm.S)
    endif()
else()
    message(FATAL_ERROR "unknown PCSXAB_BUILTIN_GPU '${PCSXAB_BUILTIN_GPU}'")
endif()
set_property(SOURCE frontend/main.c libpcsxcore/misc.c APPEND PROPERTY COMPILE_DEFINITIONS BUILTIN_GPU=${PCSXAB_BUILTIN_GPU})
# the menu lists the NEON GPU wherever it can be built
if (PCSXAB_NEON OR _pcsxab_arch STREQUAL "aarch64" OR _pcsxab_arch STREQUAL "x86_64")
    set(_pcsxab_have_neon_gpu ON)
    set_property(SOURCE frontend/menu.c APPEND PROPERTY COMPILE_DEFINITIONS GPU_NEON)
else()
    set(_pcsxab_have_neon_gpu OFF)
endif()

# spu output drivers
set(_pcsxab_out_defs "")
set(_pcsxab_sound_libs "")
foreach(_drv ${_pcsxab_sound_drivers})
    if (_drv STREQUAL "sdl")
        if (PCSXAB_PLATFORM STREQUAL "headless")
            message(FATAL_ERROR "the sdl sound driver needs an SDL platform (PCSXAB_SOUND_DRIVERS= for headless)")
        endif()
        list(APPEND PCSXAB_SOURCES plugins/dfsound/sdl.c)
        list(APPEND _pcsxab_out_defs HAVE_SDL)
    elseif (_drv STREQUAL "alsa")
        list(APPEND PCSXAB_SOURCES plugins/dfsound/alsa.c)
        list(APPEND _pcsxab_out_defs HAVE_ALSA)
        list(APPEND _pcsxab_sound_libs asound)
    elseif (_drv STREQUAL "oss")
        list(APPEND PCSXAB_SOURCES plugins/dfsound/oss.c)
        list(APPEND _pcsxab_out_defs HAVE_OSS)
    elseif (_drv STREQUAL "pulseaudio")
        list(APPEND PCSXAB_SOURCES plugins/dfsound/pulseaudio.c)
        list(APPEND _pcsxab_out_defs HAVE_PULSE)
        list(APPEND _pcsxab_sound_libs pulse)
    else()
        message(FATAL_ERROR "unknown sound driver '${_drv}' in PCSXAB_SOUND_DRIVERS")
    endif()
endforeach()
set_property(SOURCE plugins/dfsound/out.c APPEND PROPERTY COMPILE_DEFINITIONS ${_pcsxab_out_defs})

if (PCSXAB_CHD)
    set_property(SOURCE libpcsxcore/cdriso.c APPEND PROPERTY COMPILE_DEFINITIONS HAVE_CHD)
    set(_pcsxab_chd_libs chdr)
endif()

add_executable(pcsx-ab ${PCSXAB_SOURCES})
target_compile_definitions(pcsx-ab PRIVATE ${_pcsxab_thread_defs})
target_include_directories(pcsx-ab PRIVATE ${_pcsxab_sdl_incs})
target_link_libraries(pcsx-ab PRIVATE ${_pcsxab_chd_libs} ${_pcsxab_sdl_libs} PNG::PNG ZLIB::ZLIB Threads::Threads
    ${CMAKE_DL_LIBS} m ${_pcsxab_sound_libs})
if (WIN32)
    # SDL's own main() wrapper is not wanted (frontend/main.c has the real one); winsock for the gdb stub
    # in libpcsxcore/socket.c, winmm for timeBeginPeriod in frontend/win32/plat_win32.c
    target_compile_definitions(pcsx-ab PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(pcsx-ab PRIVATE ws2_32 winmm)
endif()
if (PCSXAB_BUILTIN_GPU STREQUAL "unai")
    set_target_properties(pcsx-ab PROPERTIES LINKER_LANGUAGE CXX)
endif()
if (PCSXAB_LINK_MAP)
    target_link_options(pcsx-ab PRIVATE "-Wl,-Map=$<TARGET_FILE_DIR:pcsx-ab>/pcsx-ab.map")
endif()
set_target_properties(pcsx-ab PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---------------------------------------------------------------------------------------------------------
# loadable plugins (plugins/*.so next to the executable, as the frontend's plugin loader expects)
# ---------------------------------------------------------------------------------------------------------
if (PCSXAB_PLUGINS)
    set(_pcsxab_plugin_dir "${CMAKE_BINARY_DIR}/plugins")

    # gpulib once more, PIC this time, shared by every gpu plugin (plugins/gpulib/Makefile's gpulib.a)
    add_library(gpulib_pic STATIC plugins/gpulib/gpu.c plugins/gpulib/vout_pl.c plugins/gpulib/prim.c)
    set_target_properties(gpulib_pic PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME gpulib)
    target_compile_definitions(gpulib_pic PRIVATE GPULIB_PLUGIN)
    target_compile_options(gpulib_pic PRIVATE -ggdb)

    # name, sources. Each gets: -fPIC, -shared -Wl,--no-undefined, the gpulib archive.
    function(pcsxab_add_gpu_plugin name)
        add_library(${name} MODULE ${ARGN})
        set_target_properties(${name} PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${_pcsxab_plugin_dir}")
        target_compile_options(${name} PRIVATE -ggdb)
        target_link_libraries(${name} PRIVATE gpulib_pic)
        if (NOT WIN32)
            target_link_options(${name} PRIVATE -Wl,--no-undefined)
        endif()
    endfunction()

    pcsxab_add_gpu_plugin(gpu_peops plugins/dfxvideo/gpulib_if.c)
    target_compile_options(gpu_peops PRIVATE -fno-strict-aliasing)

    pcsxab_add_gpu_plugin(gpu_unai plugins/gpu_unai/gpulib_if.cpp plugins/gpu_unai/old/if.cpp)
    target_compile_definitions(gpu_unai PRIVATE REARMED USE_GPULIB=1)
    target_compile_options(gpu_unai PRIVATE -O3)
    if (_pcsxab_arch STREQUAL "arm")
        target_sources(gpu_unai PRIVATE plugins/gpu_unai/gpu_arm.S)
    endif()

    if (NOT PCSXAB_BUILTIN_GPU STREQUAL "neon" AND _pcsxab_have_neon_gpu)
        pcsxab_add_gpu_plugin(gpu_neon plugins/gpu_neon/psx_gpu_if.c)
        target_compile_definitions(gpu_neon PRIVATE NDEBUG GPU_NEON NEON_BUILD TEXTURE_CACHE_4BPP TEXTURE_CACHE_8BPP)
        if (PCSXAB_NEON)
            target_sources(gpu_neon PRIVATE plugins/gpu_neon/psx_gpu/psx_gpu_arm_neon.S)
        else()
            target_sources(gpu_neon PRIVATE plugins/gpu_neon/psx_gpu/psx_gpu_simd.c)
            target_compile_definitions(gpu_neon PRIVATE SIMD_BUILD)
            if (CMAKE_SYSTEM_PROCESSOR MATCHES "^i[3-6]86$")
                target_compile_options(gpu_neon PRIVATE -msse2) # see the built-in GPU above
            endif()
        endif()
    endif()

    add_library(spunull MODULE plugins/spunull/spunull.c)
    set_target_properties(spunull PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${_pcsxab_plugin_dir}")
    target_compile_options(spunull PRIVATE -ggdb)
endif()

# ---------------------------------------------------------------------------------------------------------
# install: the flat layout AutoBleem's Autobleem/bin/emu/ holds (pcsx-ab + plugins/*.so)
# ---------------------------------------------------------------------------------------------------------
install(TARGETS pcsx-ab RUNTIME DESTINATION .)
if (PCSXAB_PLUGINS)
    set(_pcsxab_plugin_targets gpu_peops gpu_unai spunull)
    if (TARGET gpu_neon)
        list(APPEND _pcsxab_plugin_targets gpu_neon)
    endif()
    install(TARGETS ${_pcsxab_plugin_targets} LIBRARY DESTINATION plugins)
endif()

message(STATUS "pcsx-abnxt: arch=${_pcsxab_arch} (${CMAKE_SYSTEM_PROCESSOR}) platform=${PCSXAB_PLATFORM} "
    "dynarec=${_pcsxab_dynarec} neon-asm=${PCSXAB_NEON} builtin gpu=${PCSXAB_BUILTIN_GPU} threads=${PCSXAB_THREADS} "
    "sound=${PCSXAB_SOUND_DRIVERS} chd=${PCSXAB_CHD} gles=${PCSXAB_GLES} plugins=${PCSXAB_PLUGINS} rev=${_pcsxab_rev}")
