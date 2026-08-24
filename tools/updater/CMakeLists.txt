if(NOT WIN32)
  return()
endif()

add_library(rspj_updater_logic STATIC updater_logic.cpp updater_logic.h)
target_compile_features(rspj_updater_logic PUBLIC cxx_std_17)
target_include_directories(rspj_updater_logic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(rspj_updater_logic PUBLIC Qt5::Core)

add_executable(RosPlotJugglerUpdater WIN32 main.cpp)
target_compile_features(RosPlotJugglerUpdater PRIVATE cxx_std_17)
target_link_libraries(RosPlotJugglerUpdater PRIVATE rspj_updater_logic Qt5::Core)

install(TARGETS RosPlotJugglerUpdater RUNTIME DESTINATION bin/updater)
install(FILES extract-update.ps1 DESTINATION bin/updater)

configure_file(install-manifest.json.in
               "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" DESTINATION .)

if(BUILD_TESTING)
  find_package(GTest QUIET)
  if(GTest_FOUND)
    add_executable(test_updater_logic tests/test_updater_logic.cpp)
    target_link_libraries(test_updater_logic PRIVATE rspj_updater_logic GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(test_updater_logic)

    add_executable(updater_test_health_helper tests/updater_health_helper.cpp)
    target_compile_features(updater_test_health_helper PRIVATE cxx_std_17)
    target_link_libraries(updater_test_health_helper PRIVATE Qt5::Core)
  endif()
endif()
