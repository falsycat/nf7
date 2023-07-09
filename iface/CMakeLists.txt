add_library(nf7_iface)
target_include_directories(nf7_iface PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(nf7_iface PRIVATE git_hash)

target_sources(nf7_iface
  PRIVATE
    version.cc
  PUBLIC
    version.hh
)
