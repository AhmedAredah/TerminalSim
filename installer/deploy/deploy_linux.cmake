# Linux deploy: bundle Qt6 + RabbitMQ-C + Container into the install tree, and
# patch RPATH on the executable so it finds them relative to itself.
#
# Strategy:
#   * Use install(FILES) with relative DESTINATION for non-Qt shared libs so
#     CMake/CPack handle DESTDIR staging correctly.
#   * Use install(CODE) only to invoke linuxdeploy / patchelf at install time,
#     and resolve the staging root via DESTDIR + CMAKE_INSTALL_PREFIX.

find_program(LINUXDEPLOY_BIN     linuxdeploy)
find_program(LINUXDEPLOY_QT_PLUG linuxdeploy-plugin-qt)
find_program(PATCHELF_BIN        patchelf)

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
    # Walk the symlink chain back to the real file.
    set(_real "${so_path}")
    while(IS_SYMLINK "${_real}")
        get_filename_component(_target "${_real}" REALPATH)
        if(_target STREQUAL _real)
            break()
        endif()
        set(_real "${_target}")
    endwhile()
    install(FILES "${_real}" DESTINATION lib COMPONENT Runtime)

    # Re-create the SONAME symlink chain inside the install tree.
    get_filename_component(_real_name "${_real}" NAME)
    get_filename_component(_dir "${so_path}" DIRECTORY)
    file(GLOB _aliases LIST_DIRECTORIES FALSE
         "${_dir}/$<TARGET_PROPERTY:rabbitmq::rabbitmq,OUTPUT_NAME>*")
    # Simpler: inspect the imported target's surrounding directory for matching
    # versioned symlinks and install them too.
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

# Bundle Qt6 shared libraries linked into the executable. linuxdeploy handles
# this when present, but we always copy them from the imported targets so the
# package is self-contained even on machines without linuxdeploy.
foreach(_qt_target IN ITEMS Qt6::Core Qt6::Concurrent Qt6::Network Qt6::Sql Qt6::Xml)
    if(TARGET ${_qt_target})
        _terminalsim_imported_so(${_qt_target} _qt_so)
        if(_qt_so)
            _terminalsim_install_so("${_qt_so}")
        endif()
        unset(_qt_so)
    endif()
endforeach()

# Qt6Core typically needs ICU at runtime on Linux. Install ICU libs from the Qt
# install (they are shipped alongside libQt6Core.so).
if(TARGET Qt6::Core)
    _terminalsim_imported_so(Qt6::Core _qt_core_so)
    if(_qt_core_so)
        get_filename_component(_qt_lib_dir "${_qt_core_so}" DIRECTORY)
        file(GLOB _icu_libs LIST_DIRECTORIES FALSE
             "${_qt_lib_dir}/libicu*.so*")
        foreach(_icu IN LISTS _icu_libs)
            install(FILES "${_icu}" DESTINATION lib COMPONENT Runtime)
        endforeach()
    endif()
endif()

# Run linuxdeploy / patchelf against the staged binary at install time.
install(CODE "
    set(_root \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
    set(_bin  \"\${_root}/bin/terminal_simulation\")
    if(NOT EXISTS \"\${_bin}\")
        message(STATUS \"deploy_linux: \${_bin} not present yet; skipping bundling\")
        return()
    endif()

    if(NOT \"${LINUXDEPLOY_BIN}\" STREQUAL \"LINUXDEPLOY_BIN-NOTFOUND\")
        message(STATUS \"Running linuxdeploy on \${_bin}\")
        execute_process(
            COMMAND \"${LINUXDEPLOY_BIN}\"
                    --executable=\"\${_bin}\"
                    --appdir=\"\${_root}\"
                    --plugin=qt
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(WARNING \"linuxdeploy returned \${_rc}\")
        endif()
    elseif(NOT \"${PATCHELF_BIN}\" STREQUAL \"PATCHELF_BIN-NOTFOUND\")
        message(STATUS \"linuxdeploy not found; patching RPATH manually\")
        execute_process(COMMAND \"${PATCHELF_BIN}\" --set-rpath \"\$ORIGIN/../lib\" \"\${_bin}\")
        # Bundled libraries need RPATH=\$ORIGIN so they can resolve their Qt
        # peers (e.g. libContainer -> libQt6Sql). Without this, transitive Qt
        # deps fall back to system paths and fail at runtime.
        file(GLOB _bundled_so \"\${_root}/lib/*.so*\")
        foreach(_so IN LISTS _bundled_so)
            if(NOT IS_SYMLINK \"\${_so}\")
                execute_process(COMMAND \"${PATCHELF_BIN}\" --set-rpath \"\$ORIGIN\" \"\${_so}\"
                                ERROR_QUIET)
            endif()
        endforeach()
    else()
        message(WARNING
            \"Neither linuxdeploy nor patchelf available; RPATH not bundled. \"
            \"Install one of them or set LD_LIBRARY_PATH at runtime.\")
    endif()
" COMPONENT Runtime)
