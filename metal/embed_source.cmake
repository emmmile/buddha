# Inlines core/buddha_kernel.h into metal/render.metal and writes the result both as a .metal
# file (for ahead-of-time compile checks) and as a C++ string for run-time compilation.
#
# cmake -DKERNEL=<buddha_kernel.h> -DSOURCE=<render.metal> -DOUTPUT_METAL=<file>
#       -DOUTPUT_HEADER=<file> -P embed_source.cmake

file(READ "${KERNEL}" kernel)
file(READ "${SOURCE}" source)
string(FIND "${source}" "#include \"buddha_kernel.h\"" position)
if(position EQUAL -1)
    message(FATAL_ERROR "${SOURCE} does not include buddha_kernel.h")
endif()
string(REPLACE "#include \"buddha_kernel.h\"" "${kernel}" source "${source}")
if(source MATCHES "\\)buddha_metal\"")
    message(FATAL_ERROR "Metal source contains the raw string delimiter")
endif()

file(WRITE "${OUTPUT_METAL}" "${source}")
file(WRITE "${OUTPUT_HEADER}"
     "// Generated from metal/render.metal and core/buddha_kernel.h; do not edit.\n"
     "#pragma once\n"
     "inline const char *buddha_metal_source = R\"buddha_metal(${source})buddha_metal\";\n")
