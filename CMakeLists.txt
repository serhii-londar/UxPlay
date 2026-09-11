cmake_minimum_required( VERSION 3.13 )

project( uxplay )

message( STATUS "Project name: " ${PROJECT_NAME} )

include(GNUInstallDirs)

set ( CMAKE_C_STANDARD 99)
set ( CMAKE_C_STANDARD_REQUIRED ON)
set ( CMAKE_C_EXTENSIONS ON)
set ( CMAKE_CXX_STANDARD 11 )
set ( CMAKE_CXX_STANDARD_REQUIRED ON)
set ( CMAKE_CXX_EXTENSIONS ON)


if (ZOOMFIX )
  message (STATUS "cmake option ZOOMFIX is no longer used (if needed, ZOOMFIX is automatically applied if X11 libraries are present)" )
endif()

if ( WIN32 )
  if ( MSVC )
    add_compile_definitions( _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS )
  endif()
  if ( DEFINED VCPKG_TARGET_TRIPLET )
    set( _VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}" )
  elseif ( CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)" )
    set( _VCPKG_TRIPLET "arm64-windows" )
  elseif ( CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|arm)" )
    set( _VCPKG_TRIPLET "arm-windows" )
  elseif ( CMAKE_SIZEOF_VOID_P EQUAL 8 )
    set( _VCPKG_TRIPLET "x64-windows" )
  else()
    set( _VCPKG_TRIPLET "x86-windows" )
  endif()

  if ( DEFINED VCPKG_INSTALLED_DIR AND EXISTS "${VCPKG_INSTALLED_DIR}/${_VCPKG_TRIPLET}/include" )
    include_directories( "${VCPKG_INSTALLED_DIR}/${_VCPKG_TRIPLET}/include" )
    link_directories( "${VCPKG_INSTALLED_DIR}/${_VCPKG_TRIPLET}/lib" )
  elseif ( DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/installed/${_VCPKG_TRIPLET}/include" )
    include_directories( "$ENV{VCPKG_ROOT}/installed/${_VCPKG_TRIPLET}/include" )
    link_directories( "$ENV{VCPKG_ROOT}/installed/${_VCPKG_TRIPLET}/lib" )
  elseif ( DEFINED CMAKE_TOOLCHAIN_FILE )
    get_filename_component(_VCPKG_ROOT "${CMAKE_TOOLCHAIN_FILE}/../../../" ABSOLUTE)
    if ( EXISTS "${_VCPKG_ROOT}/installed/${_VCPKG_TRIPLET}/include" )
      include_directories( "${_VCPKG_ROOT}/installed/${_VCPKG_TRIPLET}/include" )
      link_directories( "${_VCPKG_ROOT}/installed/${_VCPKG_TRIPLET}/lib" )
    endif()
  elseif ( DEFINED ENV{USERPROFILE} AND EXISTS "$ENV{USERPROFILE}/vcpkg/installed/${_VCPKG_TRIPLET}/include" )
    include_directories( "$ENV{USERPROFILE}/vcpkg/installed/${_VCPKG_TRIPLET}/include" )
    link_directories( "$ENV{USERPROFILE}/vcpkg/installed/${_VCPKG_TRIPLET}/lib" )
  elseif ( DEFINED CMAKE_PREFIX_PATH )
    include_directories( "${CMAKE_PREFIX_PATH}/include" )
    link_directories( "${CMAKE_PREFIX_PATH}/lib" )
  endif()
endif()

if ( ( UNIX AND NOT APPLE ) OR USE_X11 )
  if ( NOT NO_X11_DEPS )
  find_package( X11  )
    if ( X11_FOUND )
      message (STATUS "Will compile using X11 Libraries  (use cmake option -DNO_X11_DEPS=ON if X11 dependence is not wanted)" )
      link_libraries( ${X11_LIBRARIES} )
      include_directories( ${X11_INCLUDE_DIR} )
    else ()
      message (STATUS "X11 libraries not found, will compile without X11 dependence" )
    endif ()
  else()
    message (STATUS "will compile without X11 dependence" )
  endif()
endif()

if( UNIX AND NOT APPLE )
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(DBUS dbus-1>=1.4.12)
  if (DBUS_FOUND )
      add_definitions(-DDBUS )
      include_directories(${DBUS_INCLUDE_DIRS})
      #FreeBSD has this Library in /usr/local/lib
      find_library (DBUS_LIBRARY NAMES dbus-1 PATHS /usr/local/lib)
      if (NOT DBUS_LIBRARY)
      	 message(FATAL_ERROR "libdbus-1 not found")
      endif()
  endif()
endif()

if( UNIX AND NOT APPLE )
  # convert AirPlay colormap 1:3:7:1 to sRGB (1:1:7:1), needed on Linux and BSD
  add_definitions( -DFULL_RANGE_RGB_FIX )
else()
  set( CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE )
  if( APPLE )
    set( CMAKE_BUILD_WITH_INSTALL_RPATH TRUE )
    set( CMAKE_INSTALL_RPATH
      "@executable_path/../Frameworks"
      "@loader_path/../Frameworks"
      "/Library/Frameworks/GStreamer.framework/Libraries"
      "/Library/Frameworks/GStreamer.framework/Versions/1.0/lib"
      "/opt/homebrew/lib"
      "/usr/local/lib"
    )
  endif()
endif()

if (USE_DNS_SD OR ( APPLE AND NOT USE_MDNS ))
  message ( STATUS "******** using Bounjour/Apple dns_sd.h for Service Discovery")
  if ( APPLE )
    add_definitions( -DUXPLAY_HAVE_APPLE_P2P )
  endif()
  add_subdirectory( lib/dns_sd)
  if (UNIX AND NOT APPLE)
    add_definitions( -DSUPPRESS_AVAHI_COMPAT_WARNING )
  endif()	     
  if ( APPLE )	
       message ( STATUS "******** use \"cmake -DUSE_MDNS=1\" to build using UxPlay mdnsd code instead of Apple Bonjour" )	
  endif()
else() 
  message ( STATUS "******** using internal minimal mDNSResponder implementation (lib/mdnsd) for Service Discovery")
  if (NOT APPLE)
     message ( STATUS "******** use \"cmake -DUSE_DNS_SD=1\" to build with the previously-used Apple/Avahi dns-sd.h code")
  endif()
  add_subdirectory( lib/mdnsd)
endif()

add_subdirectory( lib/llhttp )
add_subdirectory( lib/playfair )
add_subdirectory( lib )
add_subdirectory( renderers )

if  ( GST_MACOS )
     add_definitions( -DGST_MACOS )
     message ( STATUS "define GST_MACOS" )
endif()

if( APPLE )
  link_directories(
    "/opt/homebrew/lib"
    "/Library/Frameworks/GStreamer.framework/Libraries"
    "/Library/Frameworks/GStreamer.framework/Versions/1.0/lib"
    "/usr/local/lib"
  )
endif()

add_executable( uxplay uxplay.cpp )
target_link_libraries( uxplay renderers airplay gstsdp-1.0 gstvideo-1.0 gstapp-1.0 gstbase-1.0 gstreamer-1.0 gobject-2.0 glib-2.0 intl )
if (DBUS_FOUND)
      target_link_libraries( uxplay ${DBUS_LIBRARY})
endif()

install( TARGETS  uxplay RUNTIME DESTINATION bin )
install( FILES uxplay.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 )
install( FILES README.md README.txt README.html LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} ) 
install( FILES lib/llhttp/LICENSE-MIT DESTINATION ${CMAKE_INSTALL_DOCDIR}/llhttp )
install( FILES uxplay.service DESTINATION ${CMAKE_INSTALL_DOCDIR}/systemd ) 
install( FILES Bluetooth_LE_beacon/uxplay-beacon.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 )
install( FILES Bluetooth_LE_beacon/uxplay-beacon.py
         DESTINATION bin
         PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ  WORLD_EXECUTE WORLD_READ)
install( FILES Bluetooth_LE_beacon/uxplay_beacon_module_BleuIO.py
         DESTINATION bin
         PERMISSIONS  OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)

if (DBUS_FOUND)
install( FILES Bluetooth_LE_beacon/uxplay_beacon_module_BlueZ.py
         DESTINATION bin
         PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
endif()

if (WIN32)
install( FILES Bluetooth_LE_beacon/uxplay_beacon_module_winrt.py
         DESTINATION bin
         PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
endif()

if (UNIX AND NOT APPLE)
install( FILES Bluetooth_LE_beacon/uxplay_beacon_module_HCI.py
         DESTINATION bin
         PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ)
endif()


# uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
