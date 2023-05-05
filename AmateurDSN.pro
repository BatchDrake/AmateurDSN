QT += core widgets gui

TEMPLATE = lib
DEFINES += AMATEURDSN_LIBRARY

CONFIG += c++11
CONFIG += unversioned_libname unversioned_soname

isEmpty(PLUGIN_DIRECTORY) {
  _HOME = $$(HOME)
  isEmpty(_HOME) {
    error(Cannot deduce user home directory. Please provide a valid plugin installation path through the PLUGIN_DIRECTORY property)
  }

  PLUGIN_DIRECTORY=$$_HOME/.suscan/plugins
}

isEmpty(SUWIDGETS_PREFIX) {
  SUWIDGETS_INSTALL_HEADERS=$$[QT_INSTALL_HEADERS]/SuWidgets
} else {
  SUWIDGETS_INSTALL_HEADERS=$$SUWIDGETS_PREFIX/include/SuWidgets
}

isEmpty(SIGDIGGER_PREFIX) {
  SIGDIGGER_INSTALL_HEADERS=$$[QT_INSTALL_HEADERS]/SigDigger
} else {
  SIGDIGGER_INSTALL_HEADERS=$$SIGDIGGER_PREFIX/include/SigDigger
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ChirpCorrector.cpp \
    ExternalTool.cpp \
    ExternalToolFactory.cpp \
    ForwarderWidget.cpp \
    PowerProcessor.cpp \
    ProcessForwarder.cpp \
    Registration.cpp \
    SNRTool.cpp \
    SNRToolFactory.cpp


INCLUDEPATH += $$SUWIDGETS_INSTALL_HEADERS $$SIGDIGGER_INSTALL_HEADERS

unix: CONFIG += link_pkgconfig
unix: PKGCONFIG += suscan sigutils fftw3 sndfile

CONFIG += c++11


# Default rules for deployment.
target.path = $$PLUGIN_DIRECTORY
!isEmpty(target.path): INSTALLS += target

FORMS += \
  ExternalTool.ui \
  ForwarderWidget.ui \
  SNRTool.ui

HEADERS += \
  ChirpCorrector.h \
  ExternalTool.h \
  ExternalToolFactory.h \
  ForwarderWidget.h \
  PowerProcessor.h \
  ProcessForwarder.h \
  SNRTool.h \
  SNRToolFactory.h
