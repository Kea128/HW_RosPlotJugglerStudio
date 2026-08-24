set(ROSBAG_PYTHON_WORKER
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../runtime/rosbag_python/extract_rosbag.py")
get_filename_component(ROSBAG_PYTHON_WORKER "${ROSBAG_PYTHON_WORKER}" ABSOLUTE)

if(NOT EXISTS "${ROSBAG_PYTHON_WORKER}")
  message(FATAL_ERROR "ROS bag Python worker not found: ${ROSBAG_PYTHON_WORKER}")
endif()

add_library(rosbag_record_parser STATIC
  rosbag_record_parser.cpp
  rosbag_record_parser.h)
target_include_directories(rosbag_record_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rosbag_record_parser PUBLIC Qt5::Core plotjuggler_base)

add_library(DataLoadROSBag SHARED dataload_rosbag.cpp dataload_rosbag.h)
target_link_libraries(DataLoadROSBag PRIVATE
  rosbag_record_parser
  Qt5::Widgets
  Qt5::Xml
  plotjuggler_base)
target_compile_definitions(DataLoadROSBag PRIVATE
  QT_PLUGIN
  ROSBAG_PYTHON_WORKER_SOURCE="${ROSBAG_PYTHON_WORKER}")

add_custom_command(TARGET DataLoadROSBag POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:DataLoadROSBag>/runtime/rosbag_python"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${ROSBAG_PYTHON_WORKER}"
          "$<TARGET_FILE_DIR:DataLoadROSBag>/runtime/rosbag_python/extract_rosbag.py"
  COMMENT "Packaging ROS bag Python worker")

install(TARGETS DataLoadROSBag DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY})
install(FILES "${ROSBAG_PYTHON_WORKER}"
        DESTINATION "bin/runtime/rosbag_python")

if(BUILD_TESTING)
  find_package(GTest QUIET)
  if(GTest_FOUND)
    add_executable(test_rosbag_record_parser tests/test_rosbag_record_parser.cpp)
    target_link_libraries(test_rosbag_record_parser PRIVATE
      rosbag_record_parser
      GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(test_rosbag_record_parser)
  endif()
endif()