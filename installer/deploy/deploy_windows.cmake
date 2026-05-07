# Windows deploy: run windeployqt against the installed binary so all required
# Qt DLLs and plugins are copied into bin/. Also drop RabbitMQ-C and Container
# DLLs next to the executable.
#
# windeployqt only follows the Qt imports of the binaries you point it at, so
# we feed it Container.dll alongside terminal_simulation.exe. Qt modules pulled
# in transitively via Container (e.g. Qt6Sql for the SQLite-backed container
# store) would otherwise be missed and the installed app would fail to start
# with STATUS_DLL_NOT_FOUND (0xC0000135).

find_program(WINDEPLOYQT_BIN
    NAMES windeployqt windeployqt6
    HINTS "${Qt6_DIR}/../../../bin")

function(_terminalsim_imported_dll target out_var)
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

_terminalsim_imported_dll(rabbitmq::rabbitmq    _rabbit_dll)
_terminalsim_imported_dll(Container::Container  _container_dll)

if(_rabbit_dll AND EXISTS "${_rabbit_dll}")
    install(FILES "${_rabbit_dll}" DESTINATION bin COMPONENT Runtime)
endif()
if(_container_dll AND EXISTS "${_container_dll}")
    install(FILES "${_container_dll}" DESTINATION bin COMPONENT Runtime)
endif()

install(CODE "
    set(_root   \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
    set(_bin    \"\${_root}/bin/terminal_simulation.exe\")
    if(NOT EXISTS \"\${_bin}\")
        message(STATUS \"deploy_windows: \${_bin} not present yet; skipping bundling\")
        return()
    endif()

    # Container.dll links Qt modules (e.g. Qt6Sql) that the main executable
    # does not import directly. Pass it to windeployqt as an extra input so
    # its Qt closure ends up in the bundle.
    set(_extra_targets)
    set(_container_installed \"\${_root}/bin/Container.dll\")
    if(EXISTS \"\${_container_installed}\")
        list(APPEND _extra_targets \"\${_container_installed}\")
    endif()

    if(NOT \"${WINDEPLOYQT_BIN}\" STREQUAL \"WINDEPLOYQT_BIN-NOTFOUND\")
        message(STATUS \"Running windeployqt on \${_bin} \${_extra_targets}\")
        execute_process(
            COMMAND \"${WINDEPLOYQT_BIN}\"
                    --release
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --compiler-runtime
                    \"\${_bin}\"
                    \${_extra_targets}
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(WARNING \"windeployqt returned \${_rc}\")
        endif()
    else()
        message(WARNING \"windeployqt not found; Qt DLLs will not be bundled.\")
    endif()
" COMPONENT Runtime)
