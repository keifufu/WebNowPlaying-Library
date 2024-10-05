cmake_minimum_required(VERSION 3.29)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)

project(libwnp VERSION 3.0.0)

option(BUILD_EXAMPLES "Build examples in ./examples" OFF)

set(SRC_FILES
  src/wnp.c
  src/cws.c
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  list(APPEND SRC_FILES src/linux.c src/stb_image.c)
  add_compile_definitions(WNP_BUILD_PLATFORM_LINUX)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  list(APPEND SRC_FILES src/darwin.m)
  add_compile_definitions(WNP_BUILD_PLATFORM_DARWIN)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  list(APPEND SRC_FILES src/windows.cpp)
  add_compile_definitions(WNP_BUILD_PLATFORM_WINDOWS)
  add_compile_definitions(_WIN32_WINNT=0x0A00)
else()
  message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

set(CMAKE_STATIC_LIBRARY_PREFIX "")
add_library(${PROJECT_NAME} STATIC ${SRC_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 1
  PUBLIC_HEADER include/wnp.h
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/deps>
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  find_package(PkgConfig REQUIRED)
  
  pkg_check_modules(GIO REQUIRED gio-2.0)
  pkg_check_modules(GLIB REQUIRED glib-2.0)
  pkg_check_modules(GOBJECT REQUIRED gobject-2.0)

  target_link_libraries(${PROJECT_NAME}
    PUBLIC
      ${GIO_LIBRARIES}
      ${GLIB_LIBRARIES}
      ${GOBJECT_LIBRARIES}
  )

  target_include_directories(${PROJECT_NAME}
    PRIVATE
      ${GIO_INCLUDE_DIRS}
      ${GLIB_INCLUDE_DIRS}
      ${GOBJECT_INCLUDE_DIRS}
  )
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
  target_link_libraries(${PROJECT_NAME}
    PRIVATE
      ws2_32.lib
      gdiplus.lib
      ole32.lib
      oleaut32.lib
      advapi32.lib
  )
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
  target_link_libraries(${PROJECT_NAME}
    PRIVATE
      "-framework Cocoa"
      "-framework CoreFoundation"
      "-framework Foundation"
      "-F /System/Library/PrivateFrameworks"
      "-framework MediaRemote"
)
endif()

install(TARGETS ${PROJECT_NAME}
  EXPORT libwnpTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include
)

install(EXPORT libwnpTargets
  FILE libwnpTargets.cmake
  NAMESPACE libwnp::
  DESTINATION lib/cmake/libwnp
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libwnpConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/libwnpConfig.cmake"
  INSTALL_DESTINATION "lib/cmake/libwnp"
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/libwnpConfig.cmake"
  DESTINATION "lib/cmake/wnp"
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libwnp.pc
    DESTINATION lib/pkgconfig)
  configure_file(libwnp.pc.in libwnp.pc @ONLY)
endif()

if(BUILD_EXAMPLES)
  message(STATUS "Building examples...")

  file(GLOB EXAMPLE_FILES examples/*.c)

  foreach(EXAMPLE_FILE ${EXAMPLE_FILES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_FILE} NAME_WE)
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_FILE})
    target_link_libraries(${EXAMPLE_NAME} PRIVATE ${PROJECT_NAME})
    target_include_directories(${EXAMPLE_NAME}
      PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
  endforeach()
endif()
