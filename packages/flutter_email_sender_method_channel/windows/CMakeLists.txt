cmake_minimum_required(VERSION 3.14)

set(PROJECT_NAME "flutter_email_sender_method_channel")
project(${PROJECT_NAME} LANGUAGES CXX)

set(PLUGIN_NAME "fes_plugin")
set(FLUTTER_PLUGIN_TARGET "flutter_email_sender_method_channel_plugin")
set(PLUGIN_PUBLIC_INCLUDE_DIR "${CMAKE_BINARY_DIR}/fes_include")

# The path through Flutter's .plugin_symlinks can exceed MAX_PATH in MSBuild.
# Copy the public C API header to a short build-root include directory used by
# both this target and Flutter's generated plugin registrant.
file(MAKE_DIRECTORY
  "${PLUGIN_PUBLIC_INCLUDE_DIR}/flutter_email_sender_method_channel")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/flutter_email_sender_method_channel/flutter_email_sender_method_channel_plugin_c_api.h"
  "${PLUGIN_PUBLIC_INCLUDE_DIR}/flutter_email_sender_method_channel/flutter_email_sender_method_channel_plugin_c_api.h"
  COPYONLY
)

add_library(${PLUGIN_NAME} SHARED
  "eml_builder.cpp"
  "flutter_email_sender_method_channel_plugin.cpp"
  "flutter_email_sender_method_channel_plugin_c_api.cpp"
  "windows_email_launcher.cpp"
)

# Flutter links Windows plugins using the package-derived target name. Keep
# that public name as an alias while using a short real target name so MSBuild
# intermediate paths stay below the legacy MAX_PATH limit.
add_library(${FLUTTER_PLUGIN_TARGET} ALIAS ${PLUGIN_NAME})

apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_include_directories(${PLUGIN_NAME}
  PRIVATE "${PLUGIN_PUBLIC_INCLUDE_DIR}"
  INTERFACE "${PLUGIN_PUBLIC_INCLUDE_DIR}"
)
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin ole32 shell32 shlwapi)

if(BUILD_TESTING)
  add_executable(eml_builder_test
    "eml_builder.cpp"
    "eml_builder_test.cpp"
  )
  apply_standard_settings(eml_builder_test)
  target_link_libraries(eml_builder_test PRIVATE ole32)
  add_test(NAME eml_builder_test COMMAND eml_builder_test)
endif()

set(flutter_email_sender_method_channel_bundled_libraries
  ""
  PARENT_SCOPE
)
