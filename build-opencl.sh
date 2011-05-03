#!/bin/bash

git clone git://gitorious.org/qt-labs/opencl.git

cd opencl


# Add the correct options if the location of the opencl lib is not default.
# For the moment I don't compile openCL-openGL interop.
./configure -no-openclgl

make -j 4
