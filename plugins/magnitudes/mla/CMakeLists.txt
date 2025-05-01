# We build two variants of this library: mla.so is the basic version defining a single
# amplitude+magnitude type, while mlavariants.so also defines a few variants with
# different prefilters.

SET(MLA_TARGET mla)
SET(MLA_SOURCES mla.cpp)
SC_ADD_PLUGIN_LIBRARY(MLA ${MLA_TARGET} "")
SC_LINK_LIBRARIES_INTERNAL(${MLA_TARGET} client)

SET(MLAV_TARGET mlavariants)
SET(MLAV_SOURCES mla.cpp variants.cpp)
SC_ADD_PLUGIN_LIBRARY(MLAV ${MLAV_TARGET} "")
SC_LINK_LIBRARIES_INTERNAL(${MLAV_TARGET} client)

FILE(GLOB descs "${CMAKE_CURRENT_SOURCE_DIR}/descriptions/*.xml")
INSTALL(FILES ${descs} DESTINATION ${SC3_PACKAGE_APP_DESC_DIR})
