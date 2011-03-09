######################################################################
# Automatically generated by qmake (2.01a) Mon Jun 21 00:03:16 2010
######################################################################

TEMPLATE = app
TARGET =
DEPENDPATH += .
INCLUDEPATH += .

# Input
HEADERS += buddha.h \
	   buddhaGenerator.h \
	   complex.h \
	   controlWindow.h \
	   renderWindow.h \
	   staticStuff.h \
	   random.h
SOURCES += buddha.cpp \
	   buddhaGenerator.cpp \
	   controlWindow.cpp \
	   main.cpp \
	   renderWindow.cpp \
	   staticStuff.cpp

###############################################
# to enable profiling                         #
# uncomment the 3 directives below            #
# run the program as usual and exit normally  #
# run : gprof ./buddha                        #
###############################################
#QMAKE_CFLAGS += -pg
#QMAKE_CXXFLAGS += -pg
#QMAKE_LFLAGS += -pg


# to enable additional optimizations
#QMAKE_CXXFLAGS += -finline-functions -funswitch-loops -fgcse-after-reload -ffast-math \
#		  -fexpensive-optimizations -funroll-loops -frerun-loop-opt -mfpmath=sse -malign-double


