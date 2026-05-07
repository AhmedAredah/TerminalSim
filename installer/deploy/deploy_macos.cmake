# macOS deploy: run macdeployqt to bundle Qt frameworks. Also relocates the
# rabbitmq and Container dylibs into <prefix>/lib and adjusts install_name_tool
# entries so the executable resolves them via @executable_path/../lib.
#
# Code signing and notarization are gated on TERMINALSIM_SIGN_IDENTITY and
# TERMINALSIM_NOTARIZE_PROFILE. They are no-ops unless those values are set.

find_program(MACDEPLOYQT_BIN
    NAMES macdeployqt macdeployqt6
    HINTS "${Qt6_DIR}/../../../bin")
find_program(INSTALL_NAME_TOOL install_name_tool)

function(_terminalsim_imported_dylib target out_var)
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

_terminalsim_imported_dylib(rabbitmq::rabbitmq    _rabbit_dylib)
_terminalsim_imported_dylib(Container::Container  _container_dylib)

if(_rabbit_dylib AND EXISTS "${_rabbit_dylib}")
    install(FILES "${_rabbit_dylib}" DESTINATION lib COMPONENT Runtime)
endif()
if(_container_dylib AND EXISTS "${_container_dylib}")
    install(FILES "${_container_dylib}" DESTINATION lib COMPONENT Runtime)
endif()

install(CODE "
    set(_root \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
    set(_bin  \"\${_root}/bin/terminal_simulation\")
    set(_lib  \"\${_root}/lib\")
    if(NOT EXISTS \"\${_bin}\")
        message(STATUS \"deploy_macos: \${_bin} not present yet; skipping bundling\")
        return()
    endif()

    foreach(_dy \"${_rabbit_dylib}\" \"${_container_dylib}\")
        if(EXISTS \"\${_dy}\")
            get_filename_component(_dy_name \"\${_dy}\" NAME)
            if(NOT \"${INSTALL_NAME_TOOL}\" STREQUAL \"INSTALL_NAME_TOOL-NOTFOUND\")
                execute_process(COMMAND \"${INSTALL_NAME_TOOL}\"
                    -change \"\${_dy}\" \"@executable_path/../lib/\${_dy_name}\" \"\${_bin}\")
            endif()
        endif()
    endforeach()

    if(NOT \"${MACDEPLOYQT_BIN}\" STREQUAL \"MACDEPLOYQT_BIN-NOTFOUND\")
        message(STATUS \"Running macdeployqt on \${_bin}\")
        execute_process(COMMAND \"${MACDEPLOYQT_BIN}\" \"\${_root}\")
    endif()

    if(DEFINED ENV{TERMINALSIM_SIGN_IDENTITY})
        message(STATUS \"Codesigning with \$ENV{TERMINALSIM_SIGN_IDENTITY}\")
        execute_process(COMMAND codesign --force --deep --options runtime
            --sign \"\$ENV{TERMINALSIM_SIGN_IDENTITY}\" \"\${_bin}\")
    endif()
" COMPONENT Runtime)
