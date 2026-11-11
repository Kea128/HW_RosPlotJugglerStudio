set(ROSBAG_PYTHON_WORKER
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../runtime/rosbag_python/extract_rosbag.py")
get_filename_component(ROSBAG_PYTHON_WORKER "${ROSBAG_PYTHON_WORKER}" ABSOLUTE)
get_filename_component(ROSBAG_PYTHON_RUNTIME "${ROSBAG_PYTHON_WORKER}" DIRECTORY)
set(ROSBAG_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../rosbag/generate_numeric_ros1_sample.py")
get_filename_component(ROSBAG_GENERATOR "${ROSBAG_GENERATOR}" ABSOLUTE)
set(ROSBAG_BENCHMARK_GENERATOR
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../rosbag/generate_benchmark_bags.py")
get_filename_component(ROSBAG_BENCHMARK_GENERATOR "${ROSBAG_BENCHMARK_GENERATOR}" ABSOLUTE)

foreach(REQUIRED_FILE IN ITEMS
    "${ROSBAG_PYTHON_WORKER}"
    "${ROSBAG_GENERATOR}"
    "${ROSBAG_BENCHMARK_GENERATOR}")
  if(NOT EXISTS "${REQUIRED_FILE}")
    message(FATAL_ERROR "Required ROS bag file not found: ${REQUIRED_FILE}")
  endif()
endforeach()

add_library(rosbag_record_parser STATIC
  rosbag_binary_decoder.cpp
  rosbag_binary_decoder.h
  rosbag_raw_decoder.cpp
  rosbag_raw_decoder.h
  rosbag_record_parser.cpp
  rosbag_record_parser.h)
target_include_directories(rosbag_record_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rosbag_record_parser PUBLIC Qt5::Core plotjuggler_base)

add_library(DataLoadROSBag SHARED
  dataload_rosbag.cpp
  dataload_rosbag.h
  rosbag_topic_dialog.cpp
  rosbag_topic_dialog.h)
target_link_libraries(DataLoadROSBag PRIVATE
  rosbag_record_parser
  Qt5::Concurrent
  Qt5::Widgets
  Qt5::Xml
  plotjuggler_base)
target_compile_definitions(DataLoadROSBag PRIVATE
  QT_PLUGIN
  ROSBAG_PYTHON_WORKER_SOURCE="${ROSBAG_PYTHON_WORKER}")

install(TARGETS DataLoadROSBag DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY})
# The worker imports the vendored rosbags package and APSW shim from its own directory.
# "bin" only holds console launchers that pip writes locally for the runtime snapshot.
install(DIRECTORY "${ROSBAG_PYTHON_RUNTIME}/"
        DESTINATION "bin/runtime/rosbag_python"
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
        PATTERN "bin" EXCLUDE)

if(BUILD_TESTING)
  find_package(GTest QUIET)
  if(GTest_FOUND)
    add_executable(test_rosbag_record_parser tests/test_rosbag_record_parser.cpp)
    target_link_libraries(test_rosbag_record_parser PRIVATE
      rosbag_record_parser
      GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(test_rosbag_record_parser)

    add_executable(test_rosbag_binary_decoder tests/test_rosbag_binary_decoder.cpp)
    target_link_libraries(test_rosbag_binary_decoder PRIVATE
      rosbag_record_parser
      GTest::gtest_main)
    gtest_discover_tests(test_rosbag_binary_decoder)

    add_executable(test_rosbag_worker_e2e tests/test_rosbag_worker_e2e.cpp)
    get_filename_component(ROSBAG_ROS2_FIXTURE
      "${CMAKE_CURRENT_SOURCE_DIR}/../../../../rosbag/ros2/cdr_test/cdr_test_0.db3" ABSOLUTE)
    target_compile_definitions(test_rosbag_worker_e2e PRIVATE
      ROSBAG_PYTHON_WORKER_SOURCE="${ROSBAG_PYTHON_WORKER}"
      ROSBAG_GENERATOR_SOURCE="${ROSBAG_GENERATOR}"
      ROSBAG_BENCHMARK_GENERATOR_SOURCE="${ROSBAG_BENCHMARK_GENERATOR}"
      ROSBAG_ROS2_FIXTURE="${ROSBAG_ROS2_FIXTURE}")
    target_link_libraries(test_rosbag_worker_e2e PRIVATE
      rosbag_record_parser
      parser_ros_core
      GTest::gtest_main)
    set(ROSBAG_E2E_PYTHON "")
    if(RSPJ_PYTHON AND EXISTS "${RSPJ_PYTHON}")
      set(ROSBAG_E2E_PYTHON "${RSPJ_PYTHON}")
    elseif(DEFINED ENV{RSPJ_PYTHON} AND EXISTS "$ENV{RSPJ_PYTHON}")
      set(ROSBAG_E2E_PYTHON "$ENV{RSPJ_PYTHON}")
    else()
      get_filename_component(ROSBAG_E2E_PYTHON_HINT "${CMAKE_COMMAND}" DIRECTORY)
      foreach(ROSBAG_E2E_PYTHON_NAME python.exe python3 python)
        if(EXISTS "${ROSBAG_E2E_PYTHON_HINT}/${ROSBAG_E2E_PYTHON_NAME}")
          set(ROSBAG_E2E_PYTHON "${ROSBAG_E2E_PYTHON_HINT}/${ROSBAG_E2E_PYTHON_NAME}")
          break()
        endif()
      endforeach()
    endif()
    if(ROSBAG_E2E_PYTHON)
      file(TO_CMAKE_PATH "${ROSBAG_E2E_PYTHON}" ROSBAG_E2E_PYTHON)
      target_compile_definitions(test_rosbag_worker_e2e PRIVATE
        ROSBAG_E2E_PYTHON="${ROSBAG_E2E_PYTHON}")
      add_test(NAME RosbagWorkerPython
               COMMAND "${ROSBAG_E2E_PYTHON}" -B
                       "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_extract_rosbag.py" -v)
      set_tests_properties(RosbagWorkerPython PROPERTIES
        ENVIRONMENT "RSPJ_WORKER_RUNTIME=${ROSBAG_PYTHON_RUNTIME}")
    endif()
    gtest_discover_tests(test_rosbag_worker_e2e)

    add_executable(test_rosbag_raw_decoder tests/test_rosbag_raw_decoder.cpp)
    target_link_libraries(test_rosbag_raw_decoder PRIVATE
      rosbag_record_parser
      parser_ros_core
      GTest::gtest_main)
    gtest_discover_tests(test_rosbag_raw_decoder)
  endif()
endif()