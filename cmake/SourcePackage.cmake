# Corresponding sources include the buildable dependency subsets, not upstream
# applications, benchmarks, deployment helpers or unrelated documentation.
set(nord_source_deps "${CMAKE_CURRENT_BINARY_DIR}/source-package-dependencies")
file(REMOVE_RECURSE "${nord_source_deps}")
file(MAKE_DIRECTORY "${nord_source_deps}/dpf/utils/lv2-ttl-generator" "${nord_source_deps}/dpf/utils/symbols")
foreach(part CMakeLists.txt LICENSE cmake distrho dgl)
  file(COPY "${dpf_SOURCE_DIR}/${part}" DESTINATION "${nord_source_deps}/dpf")
endforeach()
file(COPY "${dpf_SOURCE_DIR}/utils/lv2-ttl-generator/lv2_ttl_generator.c"
  DESTINATION "${nord_source_deps}/dpf/utils/lv2-ttl-generator")
file(GLOB nord_lv2_exports "${dpf_SOURCE_DIR}/utils/symbols/lv2*.def"
  "${dpf_SOURCE_DIR}/utils/symbols/lv2*.exp" "${dpf_SOURCE_DIR}/utils/symbols/lv2*.version")
file(COPY ${nord_lv2_exports} DESTINATION "${nord_source_deps}/dpf/utils/symbols")
file(COPY "${pugl_SOURCE_DIR}/" DESTINATION "${nord_source_deps}/pugl")
file(COPY "${pugl_SOURCE_DIR}/" DESTINATION "${nord_source_deps}/dpf/dgl/src/pugl-upstream")
foreach(name sst_basic_blocks sst_cpputils sst_filters sst_waveshapers)
  file(COPY "${${name}_SOURCE_DIR}/include" "${${name}_SOURCE_DIR}/LICENSE.md"
    DESTINATION "${nord_source_deps}/${name}")
endforeach()
file(COPY "${simde_SOURCE_DIR}/simde" "${simde_SOURCE_DIR}/COPYING" DESTINATION "${nord_source_deps}/simde")
set(NORD_SOURCE_DEPENDENCY_DIRECTORIES "")
foreach(name dpf pugl sst_basic_blocks sst_cpputils sst_filters sst_waveshapers simde)
  list(APPEND NORD_SOURCE_DEPENDENCY_DIRECTORIES "${nord_source_deps}/${name}" "dependency-sources/${name}")
endforeach()
