include(FetchContent)
# One source of truth: immutable commits plus archive integrity in the lock.
file(READ "${NORD_SOURCE_DIR}/dependencies.lock.json" nord_lock)
string(JSON nord_count LENGTH "${nord_lock}" components)
math(EXPR nord_last "${nord_count} - 1")
foreach(i RANGE ${nord_last})
  foreach(field cmake_name commit archive_url archive_sha256)
    string(JSON dep_${field} GET "${nord_lock}" components ${i} ${field})
  endforeach()
  string(LENGTH "${dep_commit}" commit_length)
  string(LENGTH "${dep_archive_sha256}" hash_length)
  if(NOT commit_length EQUAL 40 OR NOT dep_commit MATCHES "^[0-9a-f]+$"
      OR NOT hash_length EQUAL 64 OR NOT dep_archive_sha256 MATCHES "^[0-9a-f]+$"
      OR NOT dep_archive_url MATCHES "/${dep_commit}$")
    message(FATAL_ERROR "Invalid immutable dependency lock entry ${i}")
  endif()
  # Nord needs selected headers and DPF, not upstream examples or applications.
  # package_source contains these exact sources for offline GPL redistribution.
  if(EXISTS "${NORD_SOURCE_DIR}/dependency-sources/${dep_cmake_name}")
    string(TOUPPER "${dep_cmake_name}" dep_upper)
    set(FETCHCONTENT_SOURCE_DIR_${dep_upper} "${NORD_SOURCE_DIR}/dependency-sources/${dep_cmake_name}")
  endif()
  FetchContent_Declare(${dep_cmake_name}
    URL "${dep_archive_url}"
    URL_HASH "SHA256=${dep_archive_sha256}"
    TLS_VERIFY TRUE
    TIMEOUT 180
    INACTIVITY_TIMEOUT 30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR nord-no-upstream-project)
  FetchContent_MakeAvailable(${dep_cmake_name})
  list(APPEND NORD_SOURCE_DEPENDENCY_DIRECTORIES
    "${${dep_cmake_name}_SOURCE_DIR}" "dependency-sources/${dep_cmake_name}")
endforeach()
# GitHub archives omit submodules. Stage DPF and the locked Pugl in the
# build tree, leaving fetched sources and offline source distributions immutable.
set(NORD_DPF_SOURCE_DIR "${dpf_BINARY_DIR}/source")
file(REMOVE_RECURSE "${NORD_DPF_SOURCE_DIR}")
file(COPY "${dpf_SOURCE_DIR}/" DESTINATION "${NORD_DPF_SOURCE_DIR}")
file(COPY "${pugl_SOURCE_DIR}/" DESTINATION "${NORD_DPF_SOURCE_DIR}/dgl/src/pugl-upstream")
set(DPF_LIBRARIES OFF CACHE BOOL "No unrelated DPF libraries" FORCE)
set(DPF_EXAMPLES OFF CACHE BOOL "No DPF examples" FORCE)
add_subdirectory("${NORD_DPF_SOURCE_DIR}" "${dpf_BINARY_DIR}/build")
