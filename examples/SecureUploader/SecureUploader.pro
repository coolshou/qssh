#-------------------------------------------------
#
# Project created by QtCreator 2012-11-22T15:56:43
#
#-------------------------------------------------

QT       += core network

TARGET = SecureUploader
TEMPLATE = app

INCLUDEPATH = $$PWD/../../src/libs/ssh/

SOURCES += \
    main.cpp \
    securefileuploader.cpp

HEADERS  += \
    securefileuploader.h

include(../../qssh.pri) ## Required for IDE_LIBRARY_PATH and qtLibraryName
LIBS += \
    -L$$IDE_LIBRARY_PATH 

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
