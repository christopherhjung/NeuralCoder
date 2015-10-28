# Try to find Thorin library and include path.
# Once done this will define
#
# THORIN_INCLUDE_DIRS
# THORIN_LIBRARIES (including dependencies to LLVM/WFV2)
# THORIN_RUNTIME_DIR
# THORIN_CMAKE_DIR
# THORIN_FOUND

SET ( PROJ_NAME THORIN )

FIND_PACKAGE ( LLVM QUIET )
FIND_PACKAGE ( WFV2 QUIET )

FIND_PATH ( THORIN_ROOT_DIR thorin-config.cmake PATHS ${THORIN_DIR} $ENV{THORIN_DIR} )
SET ( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${THORIN_ROOT_DIR} )

SET ( THORIN_OUTPUT_LIBS thorin.lib thorin.so thorin.a thorin.dll thorin.dylib libthorin libthorin.so libthorin.a libthorin.dll libthorin.dylib )

FIND_PATH ( THORIN_INCLUDE_DIR NAMES thorin/world.h PATHS ${THORIN_ROOT_DIR}/src )
FIND_PATH ( THORIN_LIBS_DIR
    NAMES
        ${THORIN_OUTPUT_LIBS}
    PATHS
        ${THORIN_ROOT_DIR}/build_debug/lib
        ${THORIN_ROOT_DIR}/build_release/lib
        ${THORIN_ROOT_DIR}/build/lib
    PATH_SUFFIXES
        ${CMAKE_CONFIGURATION_TYPES}
)
FIND_PATH ( THORIN_CMAKE_DIR
    NAMES
        ThorinRuntime.cmake
    PATHS
        ${THORIN_ROOT_DIR}/build_debug/cmake
        ${THORIN_ROOT_DIR}/build_release/cmake
        ${THORIN_ROOT_DIR}/build/cmake
)
FIND_PATH ( THORIN_RUNTIME_DIR
    NAMES
        cmake/ThorinRuntime.cmake.in platforms/intrinsics_thorin.impala
    PATHS
        ${THORIN_ROOT_DIR}/runtime
)

# Include AnyDSL specific stuff
INCLUDE ( ${CMAKE_CURRENT_LIST_DIR}/thorin-shared.cmake )
FIND_LIBRARY ( THORIN_LIBRARY NAMES ${THORIN_OUTPUT_LIBS} PATHS ${THORIN_LIBS_DIR} )
GET_THORIN_DEPENDENCY_LIBS ( THORIN_TEMP_LIBS )

SET ( THORIN_LIBRARIES ${THORIN_LIBRARY} ${THORIN_TEMP_LIBS} )
SET ( THORIN_INCLUDE_DIRS ${THORIN_INCLUDE_DIR} )

INCLUDE ( FindPackageHandleStandardArgs )
FIND_PACKAGE_HANDLE_STANDARD_ARGS ( THORIN DEFAULT_MSG THORIN_LIBRARY THORIN_INCLUDE_DIR )

MARK_AS_ADVANCED ( THORIN_LIBRARY THORIN_INCLUDE_DIR THORIN_ROOT_DIR THORIN_LIBS_DIR )
