cmake_minimum_required(VERSION 3.6)

project(Plater
  VERSION      2.0
  DESCRIPTION  "3D-printer parts placer and plate generator"
  LANGUAGES    CXX
)

# Re-run cmake whenever Git index changes
set_property(GLOBAL APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/.git/index"
)

add_subdirectory(plater)
add_subdirectory(gui)

# Packaging
include(GNUInstallDirs)
set(CMAKE_INSTALL_DOCDIR ${CMAKE_INSTALL_DATAROOTDIR}/doc/plater)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git/index)
  find_package(Git REQUIRED)
  execute_process(
    COMMAND           "${GIT_EXECUTABLE}" describe --always --long --tags
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE   GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(CPACK_PACKAGE_FILE_NAME        Plater-${GIT_VERSION}-${CMAKE_SYSTEM_NAME})
  set(CPACK_SOURCE_PACKAGE_FILE_NAME Plater-${GIT_VERSION}-Source)
endif()
set(CPACK_GENERATOR 7Z)
set(CPACK_SOURCE_GENERATOR 7Z)
include(CPack)
