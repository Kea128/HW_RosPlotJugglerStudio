


qt5_wrap_ui(UI_SRC dialog_mcap.ui)

add_library(DataLoadMCAP SHARED dataload_mcap.cpp dialog_mcap.cpp ${UI_SRC})
target_include_directories(DataLoadMCAP PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)

# MCAP is compiled header-only into this plugin. MinGW cannot resolve the
# dllimport thunk generated for symbols defined by another object in the same
# DLL, so keep those internal symbols undecorated.
if(WIN32 AND MINGW)
  target_compile_definitions(DataLoadMCAP PRIVATE MCAP_PUBLIC=)
endif()

target_link_libraries(DataLoadMCAP
   PRIVATE
    LZ4::lz4_static
    zstd::libzstd_static)

target_link_libraries(DataLoadMCAP
   PRIVATE
    Qt5::Widgets
    Qt5::Xml
    Qt5::Concurrent
    plotjuggler_base)

# Suppress LNK4217 warnings on Windows for MCAP static library symbols
if(WIN32 AND MSVC)
  target_link_options(DataLoadMCAP PRIVATE /ignore:4217)
endif()

install(TARGETS DataLoadMCAP DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY})

if(BUILD_TESTING)
  find_package(GTest QUIET)
  if(GTest_FOUND)
    add_executable(test_dialog_mcap
      tests/test_dialog_mcap.cpp
      dialog_mcap.cpp
      ${UI_SRC})
    target_include_directories(test_dialog_mcap PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
      ${CMAKE_CURRENT_BINARY_DIR})
    if(WIN32 AND MINGW)
      target_compile_definitions(test_dialog_mcap PRIVATE MCAP_PUBLIC=)
    endif()
    target_link_libraries(test_dialog_mcap PRIVATE
      GTest::gtest
      Qt5::Widgets
      LZ4::lz4_static
      zstd::libzstd_static)
    add_test(NAME DialogMCAP COMMAND test_dialog_mcap)
    set_tests_properties(DialogMCAP PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
