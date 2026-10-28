# Custom function to collect source files from a specified directory
function(collect_sources source_var)
  # Initialize the sources variable
  set(sources "")

  # Iterate over the directories to include
  foreach(dir IN LISTS ARGN)
    # Recursively glob all `.c` files in the directory
    file(GLOB_RECURSE dir_sources "${dir}/*.c")

    # Append the files to the sources variable
    list(APPEND sources ${dir_sources})
  endforeach()

  # Set the sources variable in the parent scope
  set(${source_var} ${sources} PARENT_SCOPE)
endfunction()

# Define a list of directories to include
set(DIRECTORIES_TO_INCLUDE "util" "ha" "wifi" "sensor" "display" "settings" "rp2040" "btn" "mqtt" "storage" "cmd" "nav" "assets" "view_event") # Recursively access folders | only modify here could be better

# Call the custom function to collect the source files
collect_sources(COLLECTED_SOURCES ${DIRECTORIES_TO_INCLUDE})

# New deep modules should remain visible in the build manifest rather than
# relying only on recursive discovery.
list(FILTER COLLECTED_SOURCES EXCLUDE REGEX "(^|/)ha/ha_switch_sync\\.c$")
set(EXPLICIT_DOMAIN_SOURCES
  "ha/ha_switch_sync.c"
  "lvgl_health/lvgl_health.c"
  "lvgl_health/lvgl_health_state.c"
)

# Register the component
idf_component_register(
  SRCS "main.c" "lv_port.c" "indicator_model.c" "indicator_view.c"
       ${EXPLICIT_DOMAIN_SOURCES} ${COLLECTED_SOURCES}
  INCLUDE_DIRS "." ${DIRECTORIES_TO_INCLUDE}
)
