string(REGEX REPLACE "^([0-9]*[.][0-9]*)[.].*" "\\1" GNUTLS_BRANCH "${VERSION}")
vcpkg_download_distfile(tarball
    URLS
        "https://gnupg.org/ftp/gcrypt/gnutls/v${GNUTLS_BRANCH}/gnutls-${VERSION}.tar.xz"
        "https://mirrors.dotsrc.org/gcrypt/gnutls/v${GNUTLS_BRANCH}/gnutls-${VERSION}.tar.xz"
        "https://www.mirrorservice.org/sites/ftp.gnupg.org/gcrypt/gnutls/v${GNUTLS_BRANCH}/gnutls-${VERSION}.tar.xz"
    FILENAME "gnutls-${VERSION}.tar.xz"
    SHA512 332a8e5200461517c7f08515e3aaab0bec6222747422e33e9e7d25d35613e3d0695a803fce226bd6a83f723054f551328bd99dcf0573e142be777dcf358e1a3b
)
vcpkg_extract_source_archive(SOURCE_PATH
    ARCHIVE "${tarball}"
    SOURCE_BASE "v${VERSION}"
    PATCHES
        ccasflags.patch
        use-gmp-pkgconfig.patch
)

# On MSVC, <dirent.h> does not exist.  Copy a compatible implementation next
# to verify-high2.c and change the include to a local (quoted) form so it is
# found by the compiler regardless of system include paths.
if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/dirent.h"
         DESTINATION "${SOURCE_PATH}/lib/x509")
    set(_vhigh2 "${SOURCE_PATH}/lib/x509/verify-high2.c")
    if(EXISTS "${_vhigh2}")
        file(READ "${_vhigh2}" _vhigh2_content)
        string(REPLACE "#include <dirent.h>" "#include \"dirent.h\""
               _vhigh2_content "${_vhigh2_content}")
        file(WRITE "${_vhigh2}" "${_vhigh2_content}")
    endif()
endif()

vcpkg_list(SET options)

if("nls" IN_LIST FEATURES)
    vcpkg_list(APPEND options "--enable-nls")
else()
    set(ENV{AUTOPOINT} true) # true, the program
    vcpkg_list(APPEND options "--disable-nls")
endif()
if ("openssl" IN_LIST FEATURES)
    vcpkg_list(APPEND options "--enable-openssl-compatibility")
endif()

if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_list(APPEND options "LIBS=\$LIBS -liconv -lcharset") # for libunistring
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_list(APPEND options "ac_cv_dlopen_soname_works=no") # ensure vcpkg libs
endif()

set(ENV{GTKDOCIZE} true) # true, the program
set(ENV{YACC} false)     # false, the program - not used here

if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    # MSVC cannot assemble the hand-written x86-64 AES/SHA routines; disable
    # hardware acceleration so the Makefile does not try to link .obj files
    # that were never built.
    vcpkg_list(APPEND options "--disable-hardware-acceleration")

    # MSVC does not support __attribute__((constructor/destructor)).  On Windows
    # gnutls_global_init() must be called explicitly anyway, so define them away.
    set(_global_c "${SOURCE_PATH}/lib/global.c")
    if(EXISTS "${_global_c}")
        file(READ "${_global_c}" _global_c_content)
        string(REPLACE
            "#else\n#define _CONSTRUCTOR __attribute__((constructor))\n#define _DESTRUCTOR __attribute__((destructor))\n#endif"
            "#elif defined(_MSC_VER)\n#define _CONSTRUCTOR\n#define _DESTRUCTOR\n#else\n#define _CONSTRUCTOR __attribute__((constructor))\n#define _DESTRUCTOR __attribute__((destructor))\n#endif"
            _global_c_content "${_global_c_content}"
        )
        file(WRITE "${_global_c}" "${_global_c_content}")
    endif()
endif()

vcpkg_make_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    AUTORECONF
    OPTIONS
        --disable-dependency-tracking
        --disable-doc
        --disable-guile
        --disable-libdane
        --disable-maintainer-mode
        --disable-rpath
        --disable-tests
        --with-brotli=no
        --with-liboqs=no
        --with-p11-kit=no
        --with-tpm=no
        --with-tpm2=no
        --with-zlib=link
        --with-zstd=no
        ${options}
    OPTIONS_DEBUG
        --disable-tools
)

# Fix libtool quoting bug: on MSVC, lt_ar_flags contains spaces (e.g.
# "-machine:x64 -nologo cr") but is written without quotes, causing bash
# to treat "-nologo" as a command.  Quote the value in all generated libtool
# scripts so they source cleanly.
foreach(buildtype IN ITEMS dbg rel)
    set(_libtool_script "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${buildtype}/libtool")
    if(EXISTS "${_libtool_script}")
        file(READ "${_libtool_script}" _libtool_content)
        string(REGEX REPLACE
            "(lt_ar_flags=)([^\n\"'][^\n]*)"
            "\\1\"\\2\""
            _libtool_content "${_libtool_content}"
        )
        file(WRITE "${_libtool_script}" "${_libtool_content}")
    endif()
endforeach()

vcpkg_make_install()
vcpkg_fixup_pkgconfig()
vcpkg_copy_tool_dependencies("${CURRENT_PACKAGES_DIR}/tools/${PORT}/bin")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(
    COMMENT [[
The main libraries (libgnutls and libdane) are released under the
GNU Lesser General Public License version 2.1 or later
(LGPLv2+, see COPYING.LESSERv2 for the license terms), and
the gnutls-openssl extra library and the application are under the
GNU General Public License version 3 or later
(GPLv3+, see COPYING for the license terms),
unless otherwise specified in the indivual source files.
]]
    FILE_LIST
        "${SOURCE_PATH}/COPYING.LESSERv2"
        "${SOURCE_PATH}/COPYING"
)
