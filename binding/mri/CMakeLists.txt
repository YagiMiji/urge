# Binding - MRI
cmake_minimum_required(VERSION 3.20.1)

#--------------------------------------------------------------------------------
# 3rd import
#--------------------------------------------------------------------------------

## 3rd
include_directories(third_party)

## libffi
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_subdirectory(third_party/libffi)
  include_directories(third_party/libffi)
  set(MRI_ENABLE_LIBFFI ON)
else ()
  # Disable libffi on non-windows platform
  set(MRI_ENABLE_LIBFFI OFF)
endif()

## cruby interpreter
## TODO: Enhance ruby build ins
if (CMAKE_SYSTEM_NAME STREQUAL "Android")
  add_subdirectory(third_party/ruby_android)
  include_directories(third_party/ruby_android/include)
else()
  if (NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
      # Prefer bundled Ruby on Windows to avoid depending on host system Ruby.
      set(URGE_RUBY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ruby")
      set(URGE_RUBY_EXECUTABLE "${URGE_RUBY_ROOT}/bin/ruby.exe")
      file(GLOB URGE_RUBY_LIB_CANDIDATES "${URGE_RUBY_ROOT}/lib/*ruby*.lib")
      file(GLOB URGE_RUBY_INCLUDE_ROOTS LIST_DIRECTORIES true "${URGE_RUBY_ROOT}/include/ruby-*")

      if (NOT EXISTS "${URGE_RUBY_EXECUTABLE}")
        message(FATAL_ERROR "URGE Ruby Configure: Missing bundled ruby executable: ${URGE_RUBY_EXECUTABLE}")
      endif ()
      if (NOT URGE_RUBY_LIB_CANDIDATES)
        message(FATAL_ERROR "URGE Ruby Configure: Missing bundled ruby library under ${URGE_RUBY_ROOT}/lib")
      endif ()
      if (NOT URGE_RUBY_INCLUDE_ROOTS)
        message(FATAL_ERROR "URGE Ruby Configure: Missing bundled ruby include directory under ${URGE_RUBY_ROOT}/include")
      endif ()

      list(GET URGE_RUBY_LIB_CANDIDATES 0 RUBY_LIBRARY_PATH)
      list(GET URGE_RUBY_INCLUDE_ROOTS 0 URGE_RUBY_INCLUDE_ROOT)
      set(RUBY_INCLUDE_PATH "${URGE_RUBY_INCLUDE_ROOT}")

      # Find platform config include (contains ruby/config.h), e.g. x64-mswin64_140.
      file(GLOB URGE_RUBY_PLATFORM_INCLUDE_DIRS LIST_DIRECTORIES true "${URGE_RUBY_INCLUDE_ROOT}/*")
      foreach (URGE_RUBY_PLATFORM_INCLUDE_DIR IN LISTS URGE_RUBY_PLATFORM_INCLUDE_DIRS)
        if (EXISTS "${URGE_RUBY_PLATFORM_INCLUDE_DIR}/ruby/config.h")
          list(APPEND RUBY_INCLUDE_PATH "${URGE_RUBY_PLATFORM_INCLUDE_DIR}")
          break()
        endif ()
      endforeach ()
    else()
      # Find ruby headers, libs in system (non-Windows platforms).
      find_package(Ruby REQUIRED)
      set(RUBY_INCLUDE_PATH "${Ruby_INCLUDE_DIRS}")
      set(RUBY_LIBRARY_PATH "${Ruby_LIBRARIES}")
    endif ()
  else()
    file(GLOB WASM_INCLUDE_SUBDIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ruby_wasm/include/*")
    list(GET WASM_INCLUDE_SUBDIRS 0 WASM_INCLUDE_FIRST_SUBDIR)
    message(STATUS "Ruby.WASM Include Dir: ${WASM_INCLUDE_FIRST_SUBDIR}")
    set(RUBY_INCLUDE_PATH
      "${WASM_INCLUDE_FIRST_SUBDIR}"
      "${WASM_INCLUDE_FIRST_SUBDIR}/wasm32-emscripten"
    )
    set(RUBY_LIBRARY_PATH
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ruby_wasm/lib/libruby-static.a"
    )
  endif()

  # Find config file
  add_library(mri_ruby UNKNOWN IMPORTED)  
  if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Using specific configure for Windows
    # y1s1, Ruby's Windows adaptation is similar to dogshit :(
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
      message(FATAL_ERROR "URGE Ruby Configure: Unsupport architecture")
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(RUBY_INCLUDE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ruby_extern/config/"
        ${RUBY_INCLUDE_PATH}
      )
    endif ()
  endif ()

  message(STATUS "[Binding] Ruby Include Path: ${RUBY_INCLUDE_PATH}")
  message(STATUS "[Binding] Ruby Library: ${RUBY_LIBRARY_PATH}")
  
  set_target_properties(mri_ruby PROPERTIES
                        IMPORTED_LOCATION "${RUBY_LIBRARY_PATH}"
                        INTERFACE_INCLUDE_DIRECTORIES "${RUBY_INCLUDE_PATH}")
endif()

#--------------------------------------------------------------------------------
# Mri Gem Ext
#--------------------------------------------------------------------------------

if (MRI_ENABLE_LIBFFI)
  add_subdirectory(third_party/rbffi)
endif ()

add_subdirectory(third_party/zlib)

#--------------------------------------------------------------------------------
# Autogen MRI binding source collection
#--------------------------------------------------------------------------------

set(PYTHON_SCRIPT "${PROJECT_SOURCE_DIR}/binding/mri/tools/autogen.py")
set(INPUT_BINDING_DIR "${PROJECT_SOURCE_DIR}/content/public/")
set(GENERATE_BINDING_DIR "${RGU_AUTOGEN_DIR}/binding/mri/")

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" -B "${PYTHON_SCRIPT}" "${INPUT_BINDING_DIR}" "${GENERATE_BINDING_DIR}"
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  RESULT_VARIABLE SCRIPT_RESULT
)

if (NOT SCRIPT_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to create MRI binding source: ${SCRIPT_RESULT}")
endif ()

file(GLOB GENERATED_SOURCES CONFIGURE_DEPENDS "${GENERATE_BINDING_DIR}/*.cc")
file(GLOB GENERATED_HEADERS CONFIGURE_DEPENDS "${GENERATE_BINDING_DIR}/*.h")

set(BINDING_MRI_SRC
  mri_file.cc
  mri_file.h
  mri_main.cc
  mri_main.h
  mri_util.cc
  mri_util.h

  binding_patch.cc
  binding_patch.h

  platform/mri_emscripten.cc
  platform/mri_emscripten.h

  ${GENERATED_SOURCES}
  ${GENERATED_HEADERS}
)

add_library(binding_mri STATIC ${BINDING_MRI_SRC})
target_link_libraries(binding_mri
 PUBLIC
  engine_base
  engine_content
  engine_renderer
  mri_ruby
  rb_zlib
)

if (MRI_ENABLE_LIBFFI)
  target_link_libraries(binding_mri PUBLIC rb_ffi)
endif ()

if (ENABLE_ADMENRI_BUILD_VERSION)
  target_link_libraries(binding_mri PRIVATE admenri_arb)
  target_compile_definitions(binding_mri PRIVATE HAVE_GET_MACHINE_HASH=1)
endif ()
