# GeneralsX @build Codex 09/10/2026 Release path privacy without changing upstream numeric options.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/triplets/arm64-android.cmake")
if(NOT DEFINED ENV{QUEST_PUBLIC_PREFIX_FLAGS})
    message(FATAL_ERROR "Supply diagnostic prefix maps before provisioning public dependencies")
endif()
set(VCPKG_C_FLAGS "$ENV{QUEST_PUBLIC_PREFIX_FLAGS}")
set(VCPKG_CXX_FLAGS "$ENV{QUEST_PUBLIC_PREFIX_FLAGS}")
list(APPEND VCPKG_ENV_PASSTHROUGH QUEST_PUBLIC_PREFIX_FLAGS)
