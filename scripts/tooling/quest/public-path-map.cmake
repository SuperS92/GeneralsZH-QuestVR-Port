# Diagnostic path mapping only; ordinary compiler and simulation flags remain.
# Run after project() so cached CMAKE_<LANG>_FLAGS cannot discard the mappings.
if(QUEST_PUBLIC_PATH_FLAGS AND NOT QUEST_PUBLIC_PATH_MAP_APPLIED)
    add_compile_options(${QUEST_PUBLIC_PATH_FLAGS})
    set(QUEST_PUBLIC_PATH_MAP_APPLIED TRUE)
endif()
