# source files
set(COIN_GLAD_FILES
	src/glad.c
)

# build library
if(COIN_BUILD_SINGLE_LIB)
	foreach(SRCFILE ${COIN_GLAD_FILES})
		set(COIN_SOURCE ${COIN_SOURCE} rendering/${SRCFILE} CACHE INTERNAL "COIN_SOURCE")
	endforeach(SRCFILE)
else()
	add_library(glad OBJECT ${COIN_GLAD_FILES})
endif()
