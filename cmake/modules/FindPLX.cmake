# Find the PLX Library
#
# Sets the usual variables expected for find_package scripts:
#
# PLX_LIBRARY_DIR - location of PLX library.
# PLX_LIBRARIES - list of libraries to be linked.
# PLX_FOUND - true if UPAK was found.
#

find_path(PLX_LIBRARY_DIR
	NAMES libPlxApi.a
	PATHS /opt/PLX/current/PlxSdk/PlxApi/Library
			/opt/PLX/current/PlxSdk/Linux/PlxApi/Library)

# Support the REQUIRED and QUIET arguments, and set PUGIXML_FOUND if found.
include (FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS (PLX DEFAULT_MSG PLX_LIBRARY_DIR)

if (PLX_FOUND)
	set (PLX_LIBRARIES -lPlxApi -ldl) # CACHE STRING "List of PLX libraries")
endif()

mark_as_advanced (PLX_LIBRARIES PLX_LIBRARY_DIR)