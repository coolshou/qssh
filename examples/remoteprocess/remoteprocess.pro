QT += core network
TARGET=remoteprocess
SOURCES=main.cpp remoteprocesstest.cpp argumentscollector.cpp
HEADERS=remoteprocesstest.h argumentscollector.h


include(../../qssh.pri) ## Required for IDE_LIBRARY_PATH and qtLibraryName

# Don't clutter the example
DEFINES -= QT_NO_CAST_FROM_ASCII
DEFINES -= QT_NO_CAST_TO_ASCII

LIBS += -L$$IDE_LIBRARY_PATH 
uinx {
LIBS += \    
    -l$$qtLibraryName(botan-2) 
}    
win32 {    
LIBS += \
    -l$$qtLibraryName(botan-3)
}
LIBS += \
 -l$$qtLibraryName(QSsh)
