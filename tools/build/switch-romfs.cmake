function(lce_switch_romfs target)
  set(folder "${CMAKE_CURRENT_BINARY_DIR}/${target}")
  set(resources "${PROJECT_SOURCE_DIR}/resources/pc")
  set(packs "${PROJECT_SOURCE_DIR}/packs")
  set(license "${PROJECT_SOURCE_DIR}/tools/build/third_party/bearssl-LICENSE.txt")
  set(sources "${resources}/*" "${packs}/*" "${license}")
  if(ARGC GREATER 1)
    get_filename_component(assets "${ARGV1}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    if(NOT EXISTS "${assets}/Common/Media/MediaWiiU.arc")
      message(FATAL_ERROR "LCE_SWITCH_ASSETS_DIR must point at the prepared game assets directory.")
    endif()
    foreach(group IN ITEMS Common music WiiU)
      if(NOT IS_DIRECTORY "${assets}/${group}")
        message(FATAL_ERROR "Prepared game assets are missing ${group}/.")
      endif()
      list(APPEND sources "${assets}/${group}/*")
      list(APPEND copy_assets
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${assets}/${group}" "${folder}/assets/${group}")
    endforeach()
  endif()

  file(GLOB_RECURSE inputs LIST_DIRECTORIES false CONFIGURE_DEPENDS ${sources})
  set(manifest "${CMAKE_CURRENT_BINARY_DIR}/${target}.inputs")
  file(GENERATE OUTPUT "${manifest}" CONTENT "${inputs}")
  set(stamp "${CMAKE_CURRENT_BINARY_DIR}/${target}.stamp")
  add_custom_command(OUTPUT "${stamp}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${folder}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${folder}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${resources}" "${folder}/resources"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${packs}" "${folder}/packs"
    COMMAND "${CMAKE_COMMAND}" -E copy "${license}" "${folder}/"
    ${copy_assets}
    COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
    DEPENDS "${manifest}" ${inputs}
    VERBATIM)
  add_custom_target(${target} DEPENDS "${stamp}")
  set_target_properties(${target} PROPERTIES DKP_ASSET_FOLDER "${folder}" DKP_ASSET_FILES "${stamp}")
endfunction()
