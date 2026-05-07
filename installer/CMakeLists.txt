# installer/CMakeLists.txt
#
# Cross-platform packaging for TerminalSim.
#
# Opt-in via the top-level option BUILD_INSTALLER. This subproject owns:
#   * CPack configuration for native packages (DEB, RPM, TGZ, NSIS, DragNDrop)
#   * Qt Installer Framework via the CPack IFW generator (.run / .exe / .app)
#   * Runtime bundling (windeployqt, macdeployqt, linuxdeploy)
#   * Service unit files (installed but never enabled by the installer)

cmake_minimum_required(VERSION 3.20)

set(TERMINALSIM_INSTALLER_VERSION "${PROJECT_VERSION}")
set(TERMINALSIM_INSTALLER_VENDOR  "Ahmed Aredah")
set(TERMINALSIM_INSTALLER_PUBLISHER "AhmedAredah")
set(TERMINALSIM_INSTALLER_HOMEPAGE "https://github.com/AhmedAredah/TerminalSim")
set(TERMINALSIM_INSTALLER_CONTACT  "Ahmed Aredah <AhmedAredah@vt.edu>")
set(TERMINALSIM_INSTALLER_SUMMARY  "High-performance C++ container terminal simulation server")
set(TERMINALSIM_INSTALLER_LICENSE  "GPL-3.0-or-later")
set(TERMINALSIM_INSTALLER_INSTALL_ROOT "TerminalSim")
string(TIMESTAMP TERMINALSIM_INSTALLER_RELEASE_DATE "%Y-%m-%d" UTC)

set(INSTALLER_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(INSTALLER_BIN "${CMAKE_CURRENT_BINARY_DIR}")

# ----- Service unit files (Optional component) ----------------------------------
install(FILES "${INSTALLER_DIR}/service/linux/terminalsim.service"
        DESTINATION share/terminalsim/service/linux
        COMPONENT Service OPTIONAL)
install(FILES "${INSTALLER_DIR}/service/windows/install-service.ps1"
              "${INSTALLER_DIR}/service/windows/uninstall-service.ps1"
        DESTINATION share/terminalsim/service/windows
        COMPONENT Service OPTIONAL)
install(FILES "${INSTALLER_DIR}/service/macos/com.aredah.terminalsim.plist"
        DESTINATION share/terminalsim/service/macos
        COMPONENT Service OPTIONAL)

# ----- Deploy step: bundle Qt + RabbitMQ + Container ----------------------------
include("${INSTALLER_DIR}/deploy/deploy.cmake")

# ----- CPack core configuration -------------------------------------------------
include(InstallRequiredSystemLibraries)

set(CPACK_PACKAGE_NAME              "TerminalSim")
set(CPACK_PACKAGE_VENDOR            "${TERMINALSIM_INSTALLER_VENDOR}")
set(CPACK_PACKAGE_CONTACT           "${TERMINALSIM_INSTALLER_CONTACT}")
set(CPACK_PACKAGE_HOMEPAGE_URL      "${TERMINALSIM_INSTALLER_HOMEPAGE}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${TERMINALSIM_INSTALLER_SUMMARY}")
set(CPACK_PACKAGE_VERSION           "${TERMINALSIM_INSTALLER_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR     "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR     "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH     "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${TERMINALSIM_INSTALLER_INSTALL_ROOT}")
set(CPACK_PACKAGE_FILE_NAME
    "TerminalSim-${TERMINALSIM_INSTALLER_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README      "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES               TRUE)
set(CPACK_VERBATIM_VARIABLES        TRUE)

# Components shown in installer UIs.
set(CPACK_COMPONENTS_ALL Runtime Service)
set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "TerminalSim Server")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION  "TerminalSim server binary and required runtime libraries.")
set(CPACK_COMPONENT_RUNTIME_REQUIRED     TRUE)
set(CPACK_COMPONENT_SERVICE_DISPLAY_NAME "Service unit files")
set(CPACK_COMPONENT_SERVICE_DESCRIPTION
    "systemd / launchd / Windows Service definitions. Files are installed but the service is never enabled automatically.")
set(CPACK_COMPONENT_SERVICE_DEPENDS Runtime)

# ----- Generator selection ------------------------------------------------------
# IFW is included on every platform: it produces .run on Linux, .exe on Windows,
# and a .app/.dmg-style installer on macOS, all from one configuration.
if(WIN32)
    set(CPACK_GENERATOR "IFW;NSIS;ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "IFW;DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "IFW;DEB;RPM;TGZ")
endif()

# Allow CI / users to skip IFW if binarycreator is unavailable.
option(TERMINALSIM_DISABLE_IFW "Disable the Qt Installer Framework generator" OFF)
if(TERMINALSIM_DISABLE_IFW)
    list(REMOVE_ITEM CPACK_GENERATOR IFW)
endif()

# Generator-specific knobs (DEB/RPM/NSIS/DragNDrop) live in a CPackOptions file
# so they only fire for the matching generator.
configure_file(
    "${INSTALLER_DIR}/cpack/CPackOptions.cmake.in"
    "${INSTALLER_BIN}/CPackOptions.cmake"
    @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${INSTALLER_BIN}/CPackOptions.cmake")

# ----- Qt Installer Framework branding (consumed by CPackIFW) -------------------
# Default install location: user's home directory rather than /opt or
# Program Files. Operators with admin privileges can still override during
# the wizard.
if(WIN32)
    set(CPACK_IFW_TARGET_DIRECTORY "@HomeDir@/TerminalSim")
elseif(APPLE)
    set(CPACK_IFW_TARGET_DIRECTORY "@HomeDir@/Applications/TerminalSim")
else()
    set(CPACK_IFW_TARGET_DIRECTORY "@HomeDir@/TerminalSim")
endif()

set(CPACK_IFW_PACKAGE_NAME              "TerminalSim")
set(CPACK_IFW_PACKAGE_TITLE             "TerminalSim ${TERMINALSIM_INSTALLER_VERSION}")
set(CPACK_IFW_PACKAGE_PUBLISHER         "${TERMINALSIM_INSTALLER_PUBLISHER}")
set(CPACK_IFW_PACKAGE_PRODUCT_URL       "${TERMINALSIM_INSTALLER_HOMEPAGE}")
set(CPACK_IFW_PACKAGE_VERSION           "${TERMINALSIM_INSTALLER_VERSION}")
set(CPACK_IFW_PACKAGE_START_MENU_DIRECTORY "TerminalSim")
set(CPACK_IFW_PACKAGE_MAINTENANCE_TOOL_NAME "TerminalSimMaintenanceTool")
set(CPACK_IFW_PACKAGE_WIZARD_STYLE      "Modern")
set(CPACK_IFW_PACKAGE_WIZARD_DEFAULT_WIDTH  640)
set(CPACK_IFW_PACKAGE_WIZARD_DEFAULT_HEIGHT 480)
set(CPACK_IFW_PACKAGE_WIZARD_SHOW_PAGE_LIST ON)
set(CPACK_IFW_PACKAGE_ALLOW_NON_ASCII_CHARACTERS ON)
set(CPACK_IFW_PACKAGE_ALLOW_SPACE_IN_PATH ON)
set(CPACK_IFW_PACKAGE_REMOVE_TARGET_DIR  ON)
set(CPACK_IFW_VERBOSE                   ON)

if(WIN32)
    set(CPACK_IFW_PACKAGE_ICON         "${INSTALLER_DIR}/assets/icon.ico")
    set(CPACK_IFW_PACKAGE_WINDOW_ICON  "${INSTALLER_DIR}/assets/icon.ico")
elseif(APPLE)
    set(CPACK_IFW_PACKAGE_ICON         "${INSTALLER_DIR}/assets/icon.icns")
    set(CPACK_IFW_PACKAGE_WINDOW_ICON  "${INSTALLER_DIR}/assets/icon.png")
else()
    set(CPACK_IFW_PACKAGE_WINDOW_ICON  "${INSTALLER_DIR}/assets/icon.png")
endif()
set(CPACK_IFW_PACKAGE_BANNER    "${INSTALLER_DIR}/assets/ifw-banner.png")
set(CPACK_IFW_PACKAGE_WATERMARK "${INSTALLER_DIR}/assets/ifw-watermark.png")

set(CPACK_IFW_PACKAGE_CONTROL_SCRIPT
    "${INSTALLER_DIR}/ifw/config/controlscript.qs")

# Allow IFW root to be overridden from the command line / environment so CI can
# point at the installed Qt Tools/QtInstallerFramework path.
if(DEFINED ENV{QTIFWDIR} AND NOT DEFINED CPACK_IFW_ROOT)
    set(CPACK_IFW_ROOT "$ENV{QTIFWDIR}")
endif()

include(CPack)

# CPackIFW must be loaded after CPack so its component configuration helpers
# can register against the generator that include(CPack) just initialized.
include(CPackIFW)

cpack_ifw_configure_component(Runtime
    ESSENTIAL FORCED_INSTALLATION
    NAME        "com.aredah.terminalsim.core"
    DISPLAY_NAME "TerminalSim Server"
    DESCRIPTION  "TerminalSim simulation server binary, Qt runtime, and bundled libraries."
    VERSION      "${TERMINALSIM_INSTALLER_VERSION}"
    LICENSES     "GNU GPLv3" "${CMAKE_SOURCE_DIR}/LICENSE"
    SCRIPT       "${INSTALLER_DIR}/ifw/packages/com.aredah.terminalsim.core/meta/installscript.qs")

cpack_ifw_configure_component(Service
    NAME        "com.aredah.terminalsim.service"
    DISPLAY_NAME "Service unit files"
    DESCRIPTION  "systemd / launchd / Windows Service definitions. Installed but never enabled by this installer."
    VERSION      "${TERMINALSIM_INSTALLER_VERSION}"
    DEPENDS      "com.aredah.terminalsim.core"
    SCRIPT       "${INSTALLER_DIR}/ifw/packages/com.aredah.terminalsim.service/meta/installscript.qs")
