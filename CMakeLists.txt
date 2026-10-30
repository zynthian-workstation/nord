cmake_minimum_required(VERSION 3.24)
project(Nord VERSION 1.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Shared dependencies are supplied by the target OS, never by a development tree.
set(CMAKE_SKIP_RPATH TRUE)
set(NORD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

include("${NORD_SOURCE_DIR}/cmake/Dependencies.cmake")
# Public release contract: configuration fails on version divergence.
execute_process(COMMAND "${Python3_EXECUTABLE}" -B "${NORD_SOURCE_DIR}/ci/validate.py"
  manifest --root "${NORD_SOURCE_DIR}" RESULT_VARIABLE nord_manifest_result)
if(NOT nord_manifest_result EQUAL 0)
  message(FATAL_ERROR "Invalid workstation.json or inconsistent project version")
endif()

add_library(nord_nsmp4 STATIC src/synth/Nsmp4Reader.cpp)
target_include_directories(nord_nsmp4 PUBLIC src/synth)
set_target_properties(nord_nsmp4 PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(nord_library STATIC src/library/InstrumentLibrary.cpp)
target_include_directories(nord_library PUBLIC src/library)
target_link_libraries(nord_library PUBLIC nord_nsmp4)
set_target_properties(nord_library PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(nord_npno STATIC src/engine/Npno.cpp src/engine/PianoEngine.cpp src/engine/PianoRenderer.cpp)
target_include_directories(nord_npno PUBLIC src/engine PRIVATE src/library)
target_link_libraries(nord_npno PUBLIC Threads::Threads nord_nsmp4)
set_target_properties(nord_npno PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(NORD_PIANO_CACHE_MIB 2048 CACHE STRING "Maximum inactive piano cache MiB (also capped at 1/4 physical RAM; 0 disables)")
if(NOT NORD_PIANO_CACHE_MIB MATCHES "^[0-9]+$")
  message(FATAL_ERROR "NORD_PIANO_CACHE_MIB must be nonnegative")
endif()
target_compile_definitions(nord_npno PRIVATE NORD_PIANO_CACHE_MIB=${NORD_PIANO_CACHE_MIB})

add_library(nord_scxt INTERFACE)
target_include_directories(nord_scxt INTERFACE
  "${sst_basic_blocks_SOURCE_DIR}/include"
  "${sst_cpputils_SOURCE_DIR}/include"
  "${sst_filters_SOURCE_DIR}/include"
  "${sst_waveshapers_SOURCE_DIR}/include"
  "${simde_SOURCE_DIR}")
target_compile_features(nord_scxt INTERFACE cxx_std_20)
add_library(nord_scxt_curves STATIC src/synth/scxt-port/mod_curves.cpp)
set_target_properties(nord_scxt_curves PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 20)
target_link_libraries(nord_scxt INTERFACE nord_scxt_curves)
add_library(nord_synth STATIC src/synth/SampleSynthEngine.cpp)
target_include_directories(nord_synth PUBLIC src/synth src/engine)
target_link_libraries(nord_synth PUBLIC nord_scxt nord_npno)
set_target_properties(nord_synth PROPERTIES POSITION_INDEPENDENT_CODE ON)

dpf_add_plugin(Nord TARGETS lv2 UI_TYPE cairo FILES_DSP src/plugin/NordPlugin.cpp FILES_UI src/plugin/NordUI.cpp)
target_include_directories(Nord PUBLIC src/plugin src/engine src/library src/synth)
target_link_libraries(Nord PRIVATE nord_npno nord_library nord_synth)
target_link_libraries(Nord-ui PRIVATE nord_library)
add_executable(nord_catalog src/library/catalog.cpp)
target_link_libraries(nord_catalog PRIVATE nord_library)
target_include_directories(nord_catalog PRIVATE src/plugin)

# A cross build ships the target catalog binary but uses the same-version host
# executable for metadata generation. DPF uses CMAKE_CROSSCOMPILING_EMULATOR
# for its target-architecture TTL exporter.
set(NORD_CATALOG_HOST_EXECUTABLE "" CACHE FILEPATH "Same-version host nord_catalog, required only for cross compilation")
set(NORD_CATALOG_EXECUTABLE "$<TARGET_FILE:nord_catalog>")
if(CMAKE_CROSSCOMPILING)
  if(NOT EXISTS "${NORD_CATALOG_HOST_EXECUTABLE}")
    message(FATAL_ERROR "Cross build requires NORD_CATALOG_HOST_EXECUTABLE from a native build of this source")
  endif()
  set(NORD_CATALOG_EXECUTABLE "${NORD_CATALOG_HOST_EXECUTABLE}")
endif()
set(NORD_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/bin/Nord.lv2")
# Release bundles contain only the built-in Pure preset. External catalogs and
# their LV2 preset metadata are managed separately, using the existing tools.
set(NORD_EMPTY_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/empty-library")
file(MAKE_DIRECTORY "${NORD_EMPTY_LIBRARY}")
add_custom_target(nord_bundle ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NORD_BUNDLE}/tools" "${NORD_BUNDLE}/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:nord_catalog>" "${NORD_BUNDLE}/tools/nord_catalog"
  COMMAND "${NORD_CATALOG_EXECUTABLE}" --control-layout "${NORD_BUNDLE}/tools/control-layout.tsv"
  COMMAND "${NORD_CATALOG_EXECUTABLE}" --panel-controls "${NORD_BUNDLE}/tools/panel-controls.tsv"
  COMMAND "${NORD_CATALOG_EXECUTABLE}" --synth-parameters "${NORD_BUNDLE}/tools/synth-parameters.tsv"
  COMMAND ${Python3_EXECUTABLE} -B "${NORD_SOURCE_DIR}/tools/assemble_bundle.py"
    --catalog-exe "${NORD_CATALOG_EXECUTABLE}" --source "${NORD_EMPTY_LIBRARY}" --bundle "${NORD_BUNDLE}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${NORD_SOURCE_DIR}/tools/assemble_bundle.py" "${NORD_SOURCE_DIR}/tools/generate_presets.py"
    "${NORD_SOURCE_DIR}/tools/control_layout.py" "${NORD_SOURCE_DIR}/tools/refresh_library.py"
    "${NORD_SOURCE_DIR}/integrations/zynthian/nord_host.py" "${NORD_SOURCE_DIR}/integrations/zynthian/nord_pages.py"
    "${NORD_SOURCE_DIR}/integrations/zynthian/configure_zynthian_nord_ui.py"
    "${NORD_SOURCE_DIR}/integrations/zynthian/engine.json"
    "${NORD_SOURCE_DIR}/integrations/zynthian/installation.py"
    "${NORD_SOURCE_DIR}/integrations/zynthian/installation_recipe.json"
    "${NORD_SOURCE_DIR}/integrations/zynthian/install_zynthian.py" "${NORD_BUNDLE}/tools"
  DEPENDS nord_catalog Nord-lv2 Nord-lv2-ui
  VERBATIM)
set(NORD_LICENSE_FILES
  "${NORD_SOURCE_DIR}/LICENSE|Nord-GPL3.txt"
  "${dpf_SOURCE_DIR}/LICENSE|DPF-license.txt"
  "${pugl_SOURCE_DIR}/LICENSES/ISC.txt|Pugl-ISC.txt"
  "${pugl_SOURCE_DIR}/LICENSES/0BSD.txt|Pugl-0BSD.txt"
  "${pugl_SOURCE_DIR}/LICENSES/MIT.txt|Pugl-MIT.txt"
  "${sst_basic_blocks_SOURCE_DIR}/LICENSE.md|sst-basic-blocks-GPL3.txt"
  "${sst_cpputils_SOURCE_DIR}/LICENSE.md|sst-cpputils-MIT.txt"
  "${sst_filters_SOURCE_DIR}/LICENSE.md|sst-filters-GPL3.txt"
  "${sst_waveshapers_SOURCE_DIR}/LICENSE.md|sst-waveshapers-GPL3.txt"
  "${simde_SOURCE_DIR}/COPYING|SIMDe-MIT.txt"
  "${NORD_SOURCE_DIR}/licenses/SCXT-MIT.txt|SCXT-MIT.txt"
  "${NORD_SOURCE_DIR}/licenses/nord-format-BSD3.txt|nord-format-BSD3.txt"
  "${NORD_SOURCE_DIR}/licenses/Surge-XT-GPL3.txt|Surge-XT-GPL3.txt")
foreach(record IN LISTS NORD_LICENSE_FILES)
  string(REPLACE "|" ";" pair "${record}")
  list(GET pair 0 source)
  list(GET pair 1 destination)
  add_custom_command(TARGET nord_bundle POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${source}" "${NORD_BUNDLE}/licenses/${destination}" VERBATIM)
endforeach()
add_custom_command(TARGET nord_bundle POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NORD_SOURCE_DIR}/README.md" "${NORD_BUNDLE}/licenses/THIRD-PARTY-NOTICES.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NORD_SOURCE_DIR}/dependencies.lock.json" "${NORD_BUNDLE}/licenses/dependencies.lock.json"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NORD_SOURCE_DIR}/workstation.json" "${NORD_BUNDLE}/workstation.json"
  COMMAND ${Python3_EXECUTABLE} -B "${NORD_SOURCE_DIR}/tools/package_release.py"
    --bundle "${NORD_BUNDLE}" --architecture "${CMAKE_SYSTEM_PROCESSOR}"
  VERBATIM)
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
# Prefix denotes an LV2 search directory. Packaging stages only this product.
install(DIRECTORY "${NORD_BUNDLE}/" DESTINATION Nord.lv2 USE_SOURCE_PERMISSIONS
  PATTERN "*.tmp*" EXCLUDE PATTERN "*.stamp" EXCLUDE PATTERN ".nord-refresh.lock" EXCLUDE)

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" NORD_PACKAGE_ARCH)
if(NORD_PACKAGE_ARCH MATCHES "^(arm64|aarch64)$")
  set(NORD_PACKAGE_ARCH aarch64)
elseif(NORD_PACKAGE_ARCH MATCHES "^(amd64|x86_64)$")
  set(NORD_PACKAGE_ARCH x86_64)
endif()
set(CPACK_GENERATOR TGZ)
set(CPACK_PACKAGE_NAME Nord-LV2)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "Nord-LV2-${PROJECT_VERSION}-linux-${NORD_PACKAGE_ARCH}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "Nord-LV2-${PROJECT_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES "/[.]git/;__pycache__;[.]pyc$;/build[^/]*/;/dist/;[.]tar[.]gz$;[.]wav$;[.]npno$;[.]nsmp4$")
include("${NORD_SOURCE_DIR}/cmake/SourcePackage.cmake")
set(CPACK_SOURCE_INSTALLED_DIRECTORIES "${NORD_SOURCE_DIR};/;${NORD_SOURCE_DEPENDENCY_DIRECTORIES}")
include(CPack)
