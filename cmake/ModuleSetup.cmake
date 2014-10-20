# Tundra build macros
#
# Generally used as follows:
# 1. call configure_${PACKAGE}() once from the main CMakeLists.txt
# 2. call init_target (${NAME}) once per build target in the module CMakeLists.txt
# 3. call use_package (${PACKAGE}) once per build target
# 3. call use_modules() with a list of local module names for includes
# 4. call build_library/executable() on the source files
# 5. call link_package (${PACKAGE}) once per build target
# 6. call link_modules() with a list of local module names libraries
# 7. call SetupCompileFlags/SetupCompileFlagsWithPCH
# 8. call final_target () at the end of build target's cmakelists.txt
#    (not needed for lib targets, only exe's & modules)


# =============================================================================
# reusable macros

# define target name, and directory, if it should be output
# ARGV1 is directive to output, and ARGV2 is where to
macro (init_target NAME)

    # Define target name and output directory.
    # Skip ARGV1 that is the keyword OUTPUT.
    set (TARGET_NAME ${NAME})
    set (TARGET_OUTPUT ${ARGV2})
    
    message("* " ${TARGET_NAME}) # "** " is used for dep prints so differentiate from them

    # Headers or libraries are found here will just work
    # Removed from windows due we dont have anything here directly.
    # On linux this might have useful things but still ConfigurePackages should
    # find them properly so essentially this is not needed.
    if (NOT WIN32 AND NOT APPLE)
        include_directories (${ENV_TUNDRA_DEP_PATH}/include)
        link_directories (${ENV_TUNDRA_DEP_PATH}/lib)
    endif ()
    
    # Include our own module path. This makes #include "x.h" 
    # work in project subfolders to include the main directory headers
    # note: CMAKE_INCLUDE_CURRENT_DIR could automate this
    include_directories (${CMAKE_CURRENT_SOURCE_DIR})
    
    # Add the SDK static libs build location for linking
    link_directories (${PROJECT_BINARY_DIR}/lib)
    
    # set TARGET_DIR
    if (NOT "${TARGET_OUTPUT}" STREQUAL "")
        set (TARGET_DIR ${PROJECT_BINARY_DIR}/bin/${TARGET_OUTPUT})
        if (MSVC)
            # export symbols, copy needs to be added via copy_target
            add_definitions (-DMODULE_EXPORTS)            
        endif ()
    endif ()
endmacro (init_target)

macro (final_target)
    # set TARGET_DIR
    if (TARGET_DIR)
        if (MSVC)
            # copy to target directory
            add_custom_command (TARGET ${TARGET_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E make_directory ${TARGET_DIR})
            add_custom_command (TARGET ${TARGET_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different \"$(TargetPath)\" ${TARGET_DIR})
        else ()
            # set target directory
            set_target_properties (${TARGET_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${TARGET_DIR})
            set_target_properties (${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${TARGET_DIR})
            if (APPLE)
                # Avoid putting built targets in ./bin/[RelWithDebInfo | Release | Debug] and ./bin/plugins/[RelWithDebInfo | Release | Debug]
                # Only one rule should be in place here, to avoid a bug (which is reported as fixed) in CMake where different build setups are used
                # which results already built libs to be deleted and thus causing build errors in further modules.
                # See  http://public.kitware.com/Bug/view.php?id=11844 for more info

                if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
                    set_target_properties (${TARGET_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_DEBUG ${TARGET_DIR})
                    set_target_properties (${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${TARGET_DIR})
                elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
                    set_target_properties (${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${TARGET_DIR})
                    set_target_properties (${TARGET_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${TARGET_DIR})
                elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
                    set_target_properties (${TARGET_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_RELEASE ${TARGET_DIR})
                    set_target_properties (${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${TARGET_DIR})
                endif()

                set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flat_namespace")
            endif()
        endif ()
    endif ()

    if (NOT "${PROJECT_TYPE}" STREQUAL "")
        # message (STATUS "-- project type: " ${PROJECT_TYPE})
        set_target_properties (${TARGET_NAME} PROPERTIES FOLDER ${PROJECT_TYPE})
    endif ()

    message("") # newline after each project
    
    # run the setup install macro for everything included in this build
    setup_install_target ()
    
endmacro (final_target)

# build a library from internal sources
macro (build_library TARGET_NAME LIB_TYPE)

    # On Android force all libs to be static, except the main lib (Tundra)
    if (ANDROID AND ${LIB_TYPE} STREQUAL "SHARED" AND NOT (${TARGET_NAME} STREQUAL "Tundra"))
        message("- Forcing lib to static for Android")
        set (TARGET_LIB_TYPE "STATIC")
    else ()
        set (TARGET_LIB_TYPE ${LIB_TYPE})
    endif ()

    message(STATUS "build type: " ${TARGET_LIB_TYPE} " library")
   
    # *unix add -fPIC for static libraries
    if (UNIX AND ${TARGET_LIB_TYPE} STREQUAL "STATIC")
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    endif ()

    # Version information for shared libraries.
    if (${TARGET_LIB_TYPE} STREQUAL "SHARED")
        if (WIN32)
            set(RESOURCE_FILES ${RESOURCE_FILES}
                ${PROJECT_SOURCE_DIR}/src/Core/TundraCore/Resources/resource.h
                ${PROJECT_SOURCE_DIR}/src/Core/TundraCore/Resources/TundraPlugin.rc)
        endif()
        # TODO non-Windows platforms.
    endif()

    add_library(${TARGET_NAME} ${TARGET_LIB_TYPE} ${ARGN} ${RESOURCE_FILES})

    if (MSVC AND ENABLE_BUILD_OPTIMIZATIONS)
        if (${TARGET_LIB_TYPE} STREQUAL "SHARED")
            set_target_properties (${TARGET_NAME} PROPERTIES LINK_FLAGS_RELEASE ${CMAKE_SHARED_LINKER_FLAGS_RELEASE})
            set_target_properties (${TARGET_NAME} PROPERTIES LINK_FLAGS_RELWITHDEBINFO ${CMAKE_SHARED_LINKER_FLAGS_RELEASE})
        else ()
            set_target_properties (${TARGET_NAME} PROPERTIES STATIC_LIBRARY_FLAGS_RELEASE "/LTCG")
            set_target_properties (${TARGET_NAME} PROPERTIES STATIC_LIBRARY_FLAGS_RELWITHDEBINFO "/LTCG")
        endif ()
    endif ()

    if (NOT ANDROID)
        # build static libraries to /lib if
        # - Is part of the SDK (/src/Core/)
        # - Is a static EC declared by SDK on the build (/src/EntityComponents/)
        if (${TARGET_LIB_TYPE} STREQUAL "STATIC" AND NOT TARGET_DIR)
            string (REGEX MATCH  ".*/src/Core/?.*" TARGET_IS_CORE ${CMAKE_CURRENT_SOURCE_DIR})
            string (REGEX MATCH  ".*/src/EntityComponents/?.*" TARGET_IS_EC ${CMAKE_CURRENT_SOURCE_DIR})
            if (TARGET_IS_CORE)
                message (STATUS "SDK lib output path: " ${PROJECT_BINARY_DIR}/lib)
            elseif (TARGET_IS_EC)
                message (STATUS "SDK EC lib output path: " ${PROJECT_BINARY_DIR}/lib)
            endif ()
            if (TARGET_IS_CORE OR TARGET_IS_EC)
                set_target_properties (${TARGET_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
            endif ()
        endif ()
    endif ()

    # internal library naming convention
    set_target_properties (${TARGET_NAME} PROPERTIES DEBUG_POSTFIX d)
    if (NOT ANDROID OR NOT (${TARGET_NAME} STREQUAL "Tundra"))
        set_target_properties (${TARGET_NAME} PROPERTIES PREFIX "")
    endif ()
    set_target_properties (${TARGET_NAME} PROPERTIES LINK_INTERFACE_LIBRARIES "")

endmacro (build_library)

# build an executable from internal sources 
macro (build_executable TARGET_NAME)

    set (TARGET_LIB_TYPE "EXECUTABLE")
    message (STATUS "building executable: " ${TARGET_NAME})

    add_executable (${TARGET_NAME} ${ARGN})
    if (MSVC)
        target_link_libraries (${TARGET_NAME} optimized dbghelp.lib)
    endif ()

    set_target_properties (${TARGET_NAME} PROPERTIES DEBUG_POSTFIX d)

endmacro (build_executable)

# include and lib directories, and definitions
macro (use_package PREFIX)
    message (STATUS "using " ${PREFIX})
    add_definitions (${${PREFIX}_DEFINITIONS})
    include_directories (${${PREFIX}_INCLUDE_DIRS})
    link_directories (${${PREFIX}_LIBRARY_DIRS})
endmacro (use_package)

# Macro for using modules outside the normal src/{Core|Application}/Project modules: 
# include module header dir and add link dir. Takes in list of relative paths to the modules.
# Example:      use_modules(mycompany/plugins/OurModuleOne 3rdParty/AnotherModule)
#               link_modules(OurModuleOne AnotherModule)
macro(use_modules)
    set(moduleList_ "")
    foreach (modulePath_ ${ARGN})
        include_directories (${modulePath_})
        link_directories (${modulePath_})
        if (moduleList_ STREQUAL "")
            set(moduleList_ ${module_})
        else()
            set(moduleList_ "${moduleList_}, ${module_}") 
    endforeach()
    message(STATUS "using modules: " ${moduleList_})
endmacro()

# Macro for src/Core modules: include local module headers and add link directory
# Example:      use_core_modules(Framework Input Ui)
#               link_modules(Framework Input Ui)
macro(use_core_modules)
    set(moduleList_ "")
    set (INTERNAL_MODULE_DIR ${PROJECT_SOURCE_DIR}/src/Core)
    foreach (module_ ${ARGN})
        include_directories (${INTERNAL_MODULE_DIR}/${module_})
        link_directories (${INTERNAL_MODULE_DIR}/${module_})
        if (moduleList_ STREQUAL "")
            set(moduleList_ ${module_})
        else()
            set(moduleList_ "${moduleList_}, ${module_}") 
        endif()
    endforeach ()
    message (STATUS "using Core modules: " ${moduleList_})
endmacro()

# Macro for src/Application modules: include local module headers and add link directory
# Example:      use_app_modules(JavascripModule)
#               link_modules(JavascripModule)
macro (use_app_modules)
    set(moduleList_ "")
    set (INTERNAL_MODULE_DIR ${PROJECT_SOURCE_DIR}/src/Application)
    foreach (module_ ${ARGN})
        include_directories (${INTERNAL_MODULE_DIR}/${module_})
        link_directories (${INTERNAL_MODULE_DIR}/${module_})
        if (moduleList_ STREQUAL "")
            set(moduleList_ ${module_})
        else()
            set(moduleList_ "${moduleList_}, ${module_}") 
        endif()
    endforeach ()
    message (STATUS "using Application modules: " ${moduleList_})
endmacro()

# Macro for EC include and link directory addition.
# The EC list can have items from src/EntityComponents/ or any relative path from the Tundra source tree root.
# note: You should not use this directly, use link_entity_components that will call this when needed.
# Example:      use_entity_components(EC_Sound 3rdparty/myecs/EC_Thingie)
macro (use_entity_components)
    set(moduleList_ "")
    set (INTERNAL_MODULE_DIR ${PROJECT_SOURCE_DIR}/src/EntityComponents)
    foreach (entityComponent_ ${ARGN})
        if (IS_DIRECTORY ${INTERNAL_MODULE_DIR}/${entityComponent_})
            set (_compNameInternal ${entityComponent_})
            include_directories (${INTERNAL_MODULE_DIR}/${entityComponent_})
            link_directories (${INTERNAL_MODULE_DIR}/${entityComponent_})
        elseif (IS_DIRECTORY ${PROJECT_BINARY_DIR}/${entityComponent_})
            GetLastElementFromPath(${entityComponent_} _compNameInternal)
            include_directories (${PROJECT_BINARY_DIR}/${entityComponent_})
            link_directories (${PROJECT_BINARY_DIR}/${entityComponent_})
        else ()
            message(FATAL_ERROR "Could not resolve use_entity_components() call with " ${entityComponent_} ". Are you sure the component is there?")
        endif ()
        
        if (moduleList_ STREQUAL "")
            set(moduleList_ ${_compNameInternal})
        else()
            set(moduleList_ "${moduleList_}, ${_compNameInternal}") 
        endif()
    endforeach ()
    message(STATUS "using Entity-Components: " ${moduleList_})
endmacro()

# Links the current project to the given EC, if that EC has been added to the build. Otherwise omits the EC.
# The EC list can have items from src/EntityComponents/ or any relative path from the Tundra source tree root.
# Example:      link_entity_components(EC_Sound 3rdparty/myecs/EC_Thingie)
macro(link_entity_components)
    # Link and track found components
    set (foundComponents "")
    foreach(componentName ${ARGN})
        # Determine if this is a component under the usual static EC dir
        # or a custom EC that is in a relative path.
        if (IS_DIRECTORY ${PROJECT_SOURCE_DIR}/src/EntityComponents/${componentName})
            set (_compNameInternal ${componentName})
        elseif (IS_DIRECTORY ${PROJECT_BINARY_DIR}/${componentName})
            GetLastElementFromPath(${componentName} _compNameInternal)
        else ()
            message(FATAL_ERROR "Could not resolve link_entity_components() call with " ${componentName} ". Are you sure the component is there?")
        endif ()
        
        # Check if the component is included in the build
        if (${_compNameInternal}_ENABLED)
            # Link to the project folder name
            link_modules(${_compNameInternal})
            # Add the input 'path' to list of component we are using includes from
            # 1. Its either a folder name under src/EntityComponents/componentName
            # 2. Its a relative path from project binary dir <clone>/path/componentPath
            set (foundComponents ${foundComponents} ${componentName})
            add_definitions(-D${componentName}_ENABLED)
        endif()
    endforeach ()
    # Include the ones that were found on this build
    if (foundComponents)
        use_entity_components(${foundComponents})
    endif ()
endmacro(link_entity_components)

# link directories
macro (link_package PREFIX)
    if (${PREFIX}_DEBUG_LIBRARIES)
        foreach (releaselib_  ${${PREFIX}_LIBRARIES})
            target_link_libraries (${TARGET_NAME} optimized ${releaselib_})
        endforeach ()
        foreach (debuglib_ ${${PREFIX}_DEBUG_LIBRARIES})
            target_link_libraries (${TARGET_NAME} debug ${debuglib_})
        endforeach ()
    else ()
        target_link_libraries (${TARGET_NAME} ${${PREFIX}_LIBRARIES})
    endif ()
endmacro (link_package)

# include local module libraries
macro (link_modules)
    foreach (module_ ${ARGN})
        target_link_libraries (${TARGET_NAME} ${module_})
    endforeach ()
endmacro (link_modules)

# manually find the debug libraries
macro (find_debug_libraries PREFIX DEBUG_POSTFIX)
    foreach (lib_ ${${PREFIX}_LIBRARIES})
        set (${PREFIX}_DEBUG_LIBRARIES ${${PREFIX}_DEBUG_LIBRARIES}
            ${lib_}${DEBUG_POSTFIX})
    endforeach ()
endmacro ()

# Update current translation files. 
macro (update_translation_files TRANSLATION_FILES)
	
	foreach(file ${FILES_TO_TRANSLATE})
		if(CREATED_PRO_FILE)
			FILE(APPEND ${CMAKE_CURRENT_SOURCE_DIR}/bin/data/translations/tundra_translations.pro "SOURCES += ${file} \n")
		else()
			FILE(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/bin/data/translations/tundra_translations.pro "SOURCES = ${file} \n")
			SET(CREATED_PRO_FILE "true")
		endif()
	endforeach()
	
	file (GLOB PRO_FILE bin/data/translations/*.pro)
	
	foreach(ts_file ${${TRANSLATION_FILES}})
		execute_process(COMMAND ${QT_LUPDATE_EXECUTABLE} -silent ${PRO_FILE} -ts ${ts_file} )
	endforeach()
	
	FILE(REMOVE ${CMAKE_CURRENT_SOURCE_DIR}/bin/data/translations/tundra_translations.pro)
	
endmacro()

# Update current qm files.
macro (update_qm_files TRANSLATION_FILES)
	foreach(file ${${TRANSLATION_FILES}})
		get_filename_component(name ${file} NAME_WE)
		execute_process(COMMAND ${QT_LRELEASE_EXECUTABLE} -silent ${file} -qm ${CMAKE_CURRENT_SOURCE_DIR}/bin/data/translations/${name}.qm)
	endforeach()
endmacro()

# Enables the use of Precompiled Headers in the project this macro is invoked in. Also adds the DEBUG_CPP_NAME to each .cpp file that specifies the name of that compilation unit. MSVC only.
macro(SetupCompileFlagsWithPCH)
    if (MSVC)
        # Label StableHeaders.cpp to create the PCH file and mark all other .cpp files to use that PCH file.
        # Add a #define DEBUG_CPP_NAME "this compilation unit name" to each compilation unit to aid in memory leak checking.
        foreach(src_file ${CPP_FILES})
            if (${src_file} MATCHES "StableHeaders.cpp$")
                set_source_files_properties(${src_file} PROPERTIES COMPILE_FLAGS "/YcStableHeaders.h")        
            else()
                get_filename_component(basename ${src_file} NAME)
                set_source_files_properties(${src_file} PROPERTIES COMPILE_FLAGS "/YuStableHeaders.h -DDEBUG_CPP_NAME=\"\\\"${basename}\"\\\"")
            endif()
        endforeach()
    endif()
endmacro()

# Sets up the compilation flags without PCH. For now just set the DEBUG_CPP_NAME to each compilation unit.
# TODO: The SetupCompileFlags and SetupCompileFlagsWithPCH macros should be merged, and the option to use PCH be passed in as a param. However,
# CMake string ops in PROPERTIES COMPILE_FLAGS gave some problems with this, so these are separate for now.
macro(SetupCompileFlags)
    if (MSVC)
        # Add a #define DEBUG_CPP_NAME "this compilation unit name" to each compilation unit to aid in memory leak checking.
        foreach(src_file ${CPP_FILES})
            if (${src_file} MATCHES "StableHeaders.cpp$")
            else()
                get_filename_component(basename ${src_file} NAME)
                set_source_files_properties(${src_file} PROPERTIES COMPILE_FLAGS "-DDEBUG_CPP_NAME=\"\\\"${basename}\"\\\"")
            endif()
        endforeach()
    endif()
endmacro()

# Convenience macro for including all TundraCore subfolders.
macro(UseTundraCore)
    include_directories(${PROJECT_BINARY_DIR}/src/Core/TundraCore/)
    include_directories(${PROJECT_BINARY_DIR}/src/Core/TundraCore/Asset)
    include_directories(${PROJECT_BINARY_DIR}/src/Core/TundraCore/Audio)
    include_directories(${PROJECT_BINARY_DIR}/src/Core/TundraCore/Console)
    include_directories(${PROJECT_BINARY_DIR}/src/Core/TundraCore/Framework)
    include_directories(${PROJECT_BINARY_DIR}/src/Core/TundraCore/Input)
    include_directories(${PROJECT_BINARY_DIR}/src/Core/TundraCore/Scene)
    include_directories(${PROJECT_BINARY_DIR}/src/Core/TundraCore/Script)
    include_directories(${PROJECT_BINARY_DIR}/src/Core/TundraCore/Ui)
endmacro()

# Create test executable with CTest and optionally QTest.
macro (create_test testname testsrcs testheaders)
    # Init target with provided name, eg. "Scene" > TundraTestScene
    init_target(TundraTest${testname} "./")
    
    # Remove defines that might break the build.
    # This fixes situations where create_test is called inside a
    # module CMakeLists.txt. Unfortunately rarely modules use MODULE_EXPORTS
    # in the *Api.h. If they define their own macro eg. TUNDRA_CORE_EXPORTS
    # the build will fail and you need to invoke create_test from root CMakeBuildConfig.txt
    remove_definitions (-DMODULE_EXPORTS)

    # Qt MOC for headers, we are assuming Qt based classes for QTest.
    QT4_WRAP_CPP (${testname}_MOC_SRCS ${testheaders})

    # TODO We need a way to provide additional include/link stuff, or split this function into
    # manual calls of init_target -> build_test -> final_target once tests get more advanced.
    UseTundraCore()
    use_core_modules(TundraCore Math ${ARGV3})
    use_package_knet()

    build_executable (${TARGET_NAME} ${testsrcs} ${testheaders} ${${testname}_MOC_SRCS})
    target_link_libraries (${TARGET_NAME} ${QT_QTCORE_LIBRARY} ${QT_QTGUI_LIBRARY} ${QT_QTTEST_LIBRARY})

    link_modules(TundraCore Math ${ARGV3})
    link_package_knet()

    final_target()

    # Override whatever final_target did without modifying PROJECT_TYPE.
    # We want these nicely groupped inside 'Test' folder in IDEs.
    set_target_properties (${TARGET_NAME} PROPERTIES FOLDER "Tests")

    # Let cmake/ctest know of the test executable
    add_test (NAME ${testname} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endmacro()
