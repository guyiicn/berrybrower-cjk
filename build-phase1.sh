#!/usr/bin/env bash
set -euo pipefail

build_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
docker run --rm --network none \
    --mount "type=bind,src=$build_dir,dst=/work" \
    --workdir /work/src --entrypoint /bin/bash berry-browser-builder:phase1 -c '
    set -e
    source /root/bbndk/gcc9/bb10-gcc9/env.sh

    # Match the C declaration of localeconv and keep libstdc++ math overloads.
    sed -i "s/^inline struct lconv /extern \"C\" inline struct lconv /" /root/qnx800/include/locale.h
    sed -i "/_TGEN_RC2(pow)/d; /_TGEN_R2(fmod)/d; /_TGEN_R2(fmax)/d; /_TGEN_R(round)/d; /_TGEN_RC(sqrt)/d; /_TGEN_RC( (log) )/d" /root/qnx800/include/math.h
    # Keep the C++ wrappers distinct from the C ABI functions they call.
    sed -i "/^inline void \*memchr(const void \*__s/i\#if defined(QNX_STRING_WRAPPERS_CXX_LINKAGE)\n#define QNX_WRAPPER_LINKAGE extern \"C++\"\n#else\n#define QNX_WRAPPER_LINKAGE\n#endif" /root/qnx800/include/string.h
    sed -i -E "s/^(inline (void|char) \*(memchr|strchr|strpbrk|strrchr|strstr)\(const (void|char) \*__s)/QNX_WRAPPER_LINKAGE \1/" /root/qnx800/include/string.h
    ln -s sys/elf.h /root/qnx800/include/elf.h
    sed -i "/^extern int __cxa_atexit/s/^\\(.*\\);$/#ifdef __cplusplus\\n\\1 noexcept;\\n#else\\n\\1;\\n#endif/" /root/qnx800/include/stdlib.h
    # Keep device number helpers without exporting macros into C++ accessors.
    sed -i "/^#define minor(device)/i\\#ifdef __cplusplus\\nstatic inline int minor(_CSTD dev_t device) { return (int)(device & 0x3ff); }\\nstatic inline int major(_CSTD dev_t device) { return (int)((device >> 10) & 0x3f); }\\n#else" /root/qnx800/include/sys/types.h
    sed -i "/^#define major(device)/a\\#endif" /root/qnx800/include/sys/types.h
    # Use the header bundled with Chromium when distro Clang omits it.
    install -D -m 644 third_party/llvm-build/Release+Asserts/lib/clang/18/include/fuzzer/FuzzedDataProvider.h /usr/lib/llvm-17/lib/clang/17/include/fuzzer/FuzzedDataProvider.h
    install -m 755 /work/ld-wrapper.sh /root/qnx800/arm-blackberry-qnx8eabi/bin/ld

    while IFS= read -r -d "" dotgit; do
        git config --global --add safe.directory "${dotgit%/.git}"
    done < <(find /work/src -name .git -print0 -prune)

    buildtools/linux64/gn gen out/qnx-arm
    if ! ninja -C out/qnx-arm -j 6 content/shell:content_shell > /work/phase1-build.log 2>&1; then
        tail -70 /work/phase1-build.log
        exit 1
    fi
'
