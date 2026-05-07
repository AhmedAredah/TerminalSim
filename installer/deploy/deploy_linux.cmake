# Linux deploy: bundle Qt6 + RabbitMQ-C + Container into the install tree, plus
# their transitive shared-library dependencies, and patch RPATH so the binary
# resolves everything from $ORIGIN/../lib.
#
# Strategy:
#   1. install(FILES) for the direct deps we know we need (Container,
#      rabbitmq, Qt6, ICU). CMake/CPack handle DESTDIR staging here.
#   2. install(CODE) that runs at install/cpack time:
#        a. Walks ldd of the binary and every already-bundled .so, copying any
#           non-system shared library it discovers. Iterates until the closure
#           is stable so transitive deps (libb2 -> libQt6Core, libpcre2 ->
#           libQt6Core, etc.) all land in the bundle.
#        b. patchelf --set-rpath '$ORIGIN/../lib' on the executable.
#        c. patchelf --set-rpath '$ORIGIN' on every bundled library so they
#           can resolve their peers (e.g. libContainer -> libQt6Sql).

find_program(PATCHELF_BIN patchelf)

# Resolve a shared-library path from an imported target across configurations.
function(_terminalsim_imported_so target out_var)
    foreach(prop IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_RELWITHDEBINFO
                 IMPORTED_LOCATION_MINSIZEREL IMPORTED_LOCATION_DEBUG
                 IMPORTED_LOCATION)
        get_target_property(_p ${target} ${prop})
        if(_p)
            set(${out_var} "${_p}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

_terminalsim_imported_so(rabbitmq::rabbitmq    _rabbit_so)
_terminalsim_imported_so(Container::Container  _container_so)

# Resolve symlink chains so we install the real .so plus its versioned aliases.
function(_terminalsim_install_so so_path)
    if(NOT so_path OR NOT EXISTS "${so_path}")
        return()
    endif()
    set(_real "${so_path}")
    while(IS_SYMLINK "${_real}")
        get_filename_component(_target "${_real}" REALPATH)
        if(_target STREQUAL _real)
            break()
        endif()
        set(_real "${_target}")
    endwhile()
    install(FILES "${_real}" DESTINATION lib COMPONENT Runtime)

    get_filename_component(_real_name "${_real}" NAME)
    get_filename_component(_dir "${so_path}" DIRECTORY)
    get_filename_component(_stem "${_real_name}" NAME_WE)
    file(GLOB _siblings LIST_DIRECTORIES FALSE "${_dir}/${_stem}.so*")
    foreach(_s IN LISTS _siblings)
        if(IS_SYMLINK "${_s}")
            install(FILES "${_s}" DESTINATION lib COMPONENT Runtime)
        endif()
    endforeach()
endfunction()

_terminalsim_install_so("${_rabbit_so}")
_terminalsim_install_so("${_container_so}")

foreach(_qt_target IN ITEMS Qt6::Core Qt6::Concurrent Qt6::Network Qt6::Sql Qt6::Xml)
    if(TARGET ${_qt_target})
        _terminalsim_imported_so(${_qt_target} _qt_so)
        if(_qt_so)
            _terminalsim_install_so("${_qt_so}")
        endif()
        unset(_qt_so)
    endif()
endforeach()

# ICU lives next to libQt6Core.so on most distros; bundle it explicitly.
if(TARGET Qt6::Core)
    _terminalsim_imported_so(Qt6::Core _qt_core_so)
    if(_qt_core_so)
        get_filename_component(_qt_lib_dir "${_qt_core_so}" DIRECTORY)
        file(GLOB _icu_libs LIST_DIRECTORIES FALSE "${_qt_lib_dir}/libicu*.so*")
        foreach(_icu IN LISTS _icu_libs)
            install(FILES "${_icu}" DESTINATION lib COMPONENT Runtime)
        endforeach()
    endif()
endif()

# At install time: walk ldd, pull in transitive deps, then set RPATH on
# everything. Runs both for `cmake --install` and during cpack staging.
install(CODE "
    set(_root \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
    set(_bin  \"\${_root}/bin/terminal_simulation\")
    set(_lib  \"\${_root}/lib\")
    if(NOT EXISTS \"\${_bin}\")
        message(STATUS \"deploy_linux: \${_bin} not present yet; skipping bundling\")
        return()
    endif()
    file(MAKE_DIRECTORY \"\${_lib}\")

    # System libraries that must never be bundled (glibc, dynamic loader,
    # libgcc, libstdc++, NSS, NSL, OpenSSL, X libs that pull in the world).
    set(_system_prefixes
        libc.so libm.so libdl.so libpthread.so librt.so libutil.so
        libresolv.so libnsl.so libcrypt.so libBrokenLocale.so libthread_db.so
        libanl.so libgcc_s.so libstdc++.so ld-linux libnss libcom_err.so
        linux-vdso.so
        libssl.so libcrypto.so
        libX libxcb libxkb
    )

    function(_is_system_lib name out_var)
        foreach(_p IN LISTS _system_prefixes)
            string(LENGTH \"\${_p}\" _plen)
            string(SUBSTRING \"\${name}\" 0 \${_plen} _prefix)
            if(_prefix STREQUAL _p)
                set(\${out_var} TRUE PARENT_SCOPE)
                return()
            endif()
        endforeach()
        set(\${out_var} FALSE PARENT_SCOPE)
    endfunction()

    function(_install_real so_path lib_dir)
        if(NOT EXISTS \"\${so_path}\")
            return()
        endif()
        # Walk symlink chain to the real file.
        set(_real \"\${so_path}\")
        while(IS_SYMLINK \"\${_real}\")
            get_filename_component(_t \"\${_real}\" REALPATH)
            if(_t STREQUAL _real)
                break()
            endif()
            set(_real \"\${_t}\")
        endwhile()
        get_filename_component(_real_name \"\${_real}\" NAME)
        if(NOT EXISTS \"\${lib_dir}/\${_real_name}\")
            file(COPY \"\${_real}\" DESTINATION \"\${lib_dir}\")
        endif()
        # Copy versioned symlinks alongside it.
        get_filename_component(_dir \"\${so_path}\" DIRECTORY)
        get_filename_component(_stem \"\${_real_name}\" NAME_WE)
        file(GLOB _aliases LIST_DIRECTORIES FALSE \"\${_dir}/\${_stem}.so*\")
        foreach(_a IN LISTS _aliases)
            get_filename_component(_a_name \"\${_a}\" NAME)
            if(IS_SYMLINK \"\${_a}\" AND NOT EXISTS \"\${lib_dir}/\${_a_name}\")
                file(COPY \"\${_a}\" DESTINATION \"\${lib_dir}\" FOLLOW_SYMLINK_CHAIN FALSE)
            endif()
        endforeach()
    endfunction()

    # Iteratively scan binary + bundled libs until the closure is stable.
    set(_iteration 0)
    set(_prev_count -1)
    while(TRUE)
        math(EXPR _iteration \"\${_iteration} + 1\")
        if(_iteration GREATER 6)
            message(STATUS \"deploy_linux: stopping after \${_iteration} iterations\")
            break()
        endif()
        file(GLOB _bundled LIST_DIRECTORIES FALSE \"\${_lib}/*.so*\")
        list(APPEND _bundled \"\${_bin}\")
        list(LENGTH _bundled _count)
        if(_count EQUAL _prev_count)
            break()
        endif()
        set(_prev_count \${_count})

        foreach(_target IN LISTS _bundled)
            if(IS_SYMLINK \"\${_target}\")
                continue()
            endif()
            execute_process(COMMAND ldd \"\${_target}\"
                            OUTPUT_VARIABLE _ldd_out
                            ERROR_QUIET
                            OUTPUT_STRIP_TRAILING_WHITESPACE)
            string(REPLACE \"\\n\" \";\" _lines \"\${_ldd_out}\")
            foreach(_line IN LISTS _lines)
                if(NOT _line MATCHES \"=> *(/[^ ]+)\")
                    continue()
                endif()
                set(_path \"\${CMAKE_MATCH_1}\")
                get_filename_component(_name \"\${_path}\" NAME)
                _is_system_lib(\"\${_name}\" _is_sys)
                if(_is_sys)
                    continue()
                endif()
                _install_real(\"\${_path}\" \"\${_lib}\")
            endforeach()
        endforeach()
    endwhile()

    if(\"${PATCHELF_BIN}\" STREQUAL \"PATCHELF_BIN-NOTFOUND\")
        message(WARNING \"patchelf not found; binary will rely on system Qt at runtime\")
        return()
    endif()

    # Set RPATH on the executable so it loads from <prefix>/lib.
    execute_process(COMMAND \"${PATCHELF_BIN}\" --set-rpath \"\$ORIGIN/../lib\" \"\${_bin}\")

    # Bundled libs need RPATH=\$ORIGIN so transitive Qt deps (e.g.
    # libContainer -> libQt6Sql) resolve from the same directory.
    file(GLOB _bundled_so \"\${_lib}/*.so*\")
    foreach(_so IN LISTS _bundled_so)
        if(NOT IS_SYMLINK \"\${_so}\")
            execute_process(COMMAND \"${PATCHELF_BIN}\" --set-rpath \"\$ORIGIN\" \"\${_so}\"
                            ERROR_QUIET)
        endif()
    endforeach()
" COMPONENT Runtime)
