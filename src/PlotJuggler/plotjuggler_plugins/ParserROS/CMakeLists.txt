set(INTROSPECTION_TESTS
    OFF
    CACHE BOOL "Compile the tests")

add_subdirectory(rosx_introspection)

add_library(parser_ros_core STATIC special_messages.cpp ros_parser.cpp)
set_target_properties(parser_ros_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(parser_ros_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(parser_ros_core PUBLIC Qt5::Widgets rosx_introspection
                                             plotjuggler_base data_tamer::parser
                                             fmt::fmt)

# ##############################################################################
add_library(ParserROS1 SHARED ros1_parser.h)

target_link_libraries(ParserROS1 PRIVATE parser_ros_core)

target_compile_definitions(ParserROS1 PRIVATE QT_PLUGIN)
# ##############################################################################
add_library(ParserROS2 SHARED ros2_parser.h)

target_link_libraries(ParserROS2 PRIVATE parser_ros_core)

target_compile_definitions(ParserROS2 PRIVATE QT_PLUGIN)

# ##############################################################################
add_library(ParserOMGIDL SHARED omgidl_parser.h)

target_link_libraries(ParserOMGIDL PRIVATE parser_ros_core)

target_compile_definitions(ParserOMGIDL PRIVATE QT_PLUGIN)

# ##############################################################################

install(TARGETS ParserROS1 ParserROS2 ParserOMGIDL
        DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY})
