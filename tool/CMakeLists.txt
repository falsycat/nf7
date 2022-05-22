add_executable(nf7-init)
target_link_libraries(nf7-init PRIVATE yas)
target_sources(nf7-init PRIVATE init.cc)
