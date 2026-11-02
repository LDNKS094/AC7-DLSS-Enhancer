cmake_minimum_required(VERSION 3.20)
project(AC7DLSSEnhancer VERSION 1.0.0 LANGUAGES CXX)
if(NOT WIN32)
  message(FATAL_ERROR "AC7 native rendering modules require Windows")
endif()

add_library(ac7_guides STATIC src/d3d11/guide_normalizer.cpp)
target_compile_features(ac7_guides PUBLIC cxx_std_17)
target_compile_definitions(ac7_guides PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(ac7_guides PUBLIC src/d3d11)
target_link_libraries(ac7_guides PUBLIC d3d11 d3dcompiler)

add_library(ac7_hdr STATIC src/d3d11/hdr_proxy.cpp)
target_compile_features(ac7_hdr PUBLIC cxx_std_17)
target_compile_definitions(ac7_hdr PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(ac7_hdr PRIVATE "AC7_HDR_SHADER_PATH=L\"${CMAKE_CURRENT_SOURCE_DIR}/src/d3d11/hdr_proxy.hlsl\"")
target_include_directories(ac7_hdr PUBLIC src/d3d11)
target_link_libraries(ac7_hdr PUBLIC d3d11 d3dcompiler)

add_library(ac7_hdr12 STATIC src/d3d12/hdr_proxy.cpp)
target_compile_features(ac7_hdr12 PUBLIC cxx_std_17)
target_compile_definitions(ac7_hdr12 PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(ac7_hdr12 PRIVATE "AC7_HDR_SHADER_PATH=L\"${CMAKE_CURRENT_SOURCE_DIR}/src/d3d11/hdr_proxy.hlsl\"")
target_include_directories(ac7_hdr12 PUBLIC src/d3d12)
target_link_libraries(ac7_hdr12 PUBLIC d3d12 d3dcompiler)

add_library(ac7_shared_bridge STATIC src/d3d11/shared_texture_bridge.cpp src/d3d11/guide_snapshot_pool.cpp)
target_compile_features(ac7_shared_bridge PUBLIC cxx_std_17)
target_compile_definitions(ac7_shared_bridge PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(ac7_shared_bridge PUBLIC src/d3d11)
target_link_libraries(ac7_shared_bridge PUBLIC d3d11 d3d12 dxgi)


# Native ASI implementation only.
set(AC7_MINHOOK_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/minhook" CACHE PATH "Licensed MinHook source tree")
set(NGX_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/ngx/include" CACHE PATH "Official NGX headers including NR definitions")
set(NGX_LOADER_LIBRARY "${CMAKE_SOURCE_DIR}/third_party/ngx/lib/nvsdk_ngx_d.lib" CACHE FILEPATH "Official x64 NGX loader library")
if(AC7_MINHOOK_SOURCE_DIR)
  enable_language(C)
  if(NOT EXISTS "${AC7_MINHOOK_SOURCE_DIR}/include/MinHook.h" OR NOT EXISTS "${AC7_MINHOOK_SOURCE_DIR}/LICENSE.txt")
    message(FATAL_ERROR "Provide an existing licensed MinHook source tree")
  endif()
  add_library(ac7_neutral_minhook STATIC
    "${AC7_MINHOOK_SOURCE_DIR}/src/buffer.c" "${AC7_MINHOOK_SOURCE_DIR}/src/hook.c"
    "${AC7_MINHOOK_SOURCE_DIR}/src/trampoline.c" "${AC7_MINHOOK_SOURCE_DIR}/src/hde/hde64.c")
  target_include_directories(ac7_neutral_minhook PUBLIC "${AC7_MINHOOK_SOURCE_DIR}/include")
  if(NGX_INCLUDE_DIR AND NGX_LOADER_LIBRARY)
    add_library(ac7_native_models STATIC src/d3d12/model_context.cpp src/d3d12/streamline_session.cpp src/d3d12/native_models.cpp src/d3d12/nr_capture.cpp src/d3d12/evaluation_parameters.cpp src/d3d12/nr_spatial.cpp)
    target_compile_features(ac7_native_models PUBLIC cxx_std_17)
    target_compile_definitions(ac7_native_models PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(ac7_native_models PUBLIC "${NGX_INCLUDE_DIR}" "${PROJECT_SOURCE_DIR}/third_party/streamline/include")
    target_link_libraries(ac7_native_models PUBLIC ac7_hdr12 "${NGX_LOADER_LIBRARY}" d3d11 dxgi advapi32 shlwapi version wintrust crypt32 ole32 windowscodecs)
    enable_language(ASM_MASM)
    find_program(AC7_FXC_EXECUTABLE NAMES fxc HINTS
      "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
      "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64" REQUIRED)
    set(temporal_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/temporal")
    file(MAKE_DIRECTORY "${temporal_shader_dir}")
    set(temporal_shader_headers)
    foreach(shader IN ITEMS vertex motion)
      set(profile ps_5_0)
      if(shader STREQUAL "vertex")
        set(entry Vertex)
        set(profile vs_5_0)
      else()
        set(entry Blur)
      endif()
      set(header "${temporal_shader_dir}/ac7_temporal_${shader}.h")
      add_custom_command(OUTPUT "${header}"
        COMMAND "${AC7_FXC_EXECUTABLE}" /nologo /Ges /Gis /O3 /T "${profile}" /E "${entry}"
          /Vn "ac7_temporal_${shader}" /Fh "${header}" "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d11/temporal_color.hlsl"
        DEPENDS src/d3d11/temporal_color.hlsl VERBATIM)
      list(APPEND temporal_shader_headers "${header}")
    endforeach()
    foreach(entry IN ITEMS Prepare Restore)
      set(header "${temporal_shader_dir}/ac7_hdr_${entry}.h")
      add_custom_command(OUTPUT "${header}"
        COMMAND "${AC7_FXC_EXECUTABLE}" /nologo /Ges /Gis /O3 /T cs_5_0 /E "${entry}"
          /Vn "ac7_hdr_${entry}" /Fh "${header}" "${PROJECT_SOURCE_DIR}/src/d3d11/hdr_proxy.hlsl"
        DEPENDS "${PROJECT_SOURCE_DIR}/src/d3d11/hdr_proxy.hlsl" VERBATIM)
      list(APPEND hdr_shader_headers "${header}")
    endforeach()
    add_custom_target(ac7_hdr_shaders DEPENDS ${hdr_shader_headers})
    add_dependencies(ac7_hdr12 ac7_hdr_shaders)
    target_include_directories(ac7_hdr12 PRIVATE "${temporal_shader_dir}")
    target_compile_definitions(ac7_hdr12 PRIVATE AC7_EMBED_HDR_SHADERS)
    set(rcas_header "${temporal_shader_dir}/ac7_rcas.h")
    add_custom_command(OUTPUT "${rcas_header}"
      COMMAND "${AC7_FXC_EXECUTABLE}" /nologo /Ges /Gis /O3 /T cs_5_0 /E main
        /Vn ac7_rcas /Fh "${rcas_header}" "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/rcas.hlsl"
      DEPENDS src/d3d12/rcas.hlsl VERBATIM)
    target_sources(ac7_native_models PRIVATE "${rcas_header}")
    target_include_directories(ac7_native_models PRIVATE "${temporal_shader_dir}")
    foreach(entry IN ITEMS PackColor PackGuides UnpackEdits)
      set(header "${temporal_shader_dir}/ac7_nr_${entry}.h")
      add_custom_command(OUTPUT "${header}"
        COMMAND "${AC7_FXC_EXECUTABLE}" /nologo /Ges /Gis /O3 /T cs_5_0 /E "${entry}"
          /Vn "ac7_nr_${entry}" /Fh "${header}" "${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/nr_spatial.hlsl"
        DEPENDS src/d3d12/nr_spatial.hlsl VERBATIM)
      target_sources(ac7_native_models PRIVATE "${header}")
    endforeach()
    add_library(ac7-dlss MODULE src/plugin.cpp src/d3d11/presentation_entry.cpp src/d3d11/presentation_bridge.cpp src/d3d11/final_composite.cpp src/d3d11/view_camera.cpp src/d3d11/frame_routing.cpp src/d3d11/late_sr_entry.cpp src/d3d11/resolution_hook.cpp src/d3d11/scene_rhi_queue.cpp src/d3d11/temporal_graph.cpp src/d3d11/temporal_graph_boundary.asm src/d3d11/temporal_color.cpp)
    target_sources(ac7-dlss PRIVATE ${temporal_shader_headers})
    target_include_directories(ac7-dlss PRIVATE src/d3d11 "${temporal_shader_dir}")
    target_link_libraries(ac7-dlss PRIVATE ac7_shared_bridge ac7_guides ac7_native_models ac7_neutral_minhook d3d11 dxgi)
    set_target_properties(ac7-dlss PROPERTIES PREFIX "" OUTPUT_NAME "AC7DLSS" SUFFIX ".asi")
  endif()
endif()
