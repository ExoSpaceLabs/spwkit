
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was SpWKitConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# v0.2 package metadata. This describes whether the installed library actually
# contains the hosted VSPW-TP/UDP runtime backend. Public UDP headers and backend
# identifiers are installed regardless, so consumers can keep source-compatible
# configuration code and gate runtime use explicitly.
set(SpWKit_UDP_RUNTIME_SUPPORTED ON)
set(SpWKit_UDP_RUNTIME_SCOPE "POSIX")

include("${CMAKE_CURRENT_LIST_DIR}/SpWKitTargets.cmake")

check_required_components(SpWKit)
