include(../../qssh.pri)
QT += network
QT -= gui

TARGET=shell
SOURCES=main.cpp shell.cpp argumentscollector.cpp
HEADERS=shell.h argumentscollector.h

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
