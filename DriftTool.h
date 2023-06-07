//
//    DriftTool.h: description
//    Copyright (C) 2023 Gonzalo José Carracedo Carballal
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as
//    published by the Free Software Foundation, either version 3 of the
//    License, or (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful, but
//    WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this program.  If not, see
//    <http://www.gnu.org/licenses/>
//
#ifndef DRIFTTOOL_H
#define DRIFTTOOL_H

#include <DriftToolFactory.h>
#include <WFHelpers.h>
#include <QWidget>
#include <QFile>
#include <QProcess>

namespace Ui {
  class DriftTool;
}

namespace SigDigger {
  class DriftProcessor;
  class MainSpectrum;
  class GlobalProperty;
  class DetachableProcess;

  class DriftToolConfig : public Suscan::Serializable {
  public:
    bool collapsed = false;

    std::string probeName     = "STEREO-A";
    SUFREQ      reference     = 8443518520.;
    float       lockThres     = 0.25;
    bool        retune        = true;
    float       retuneTrigger = 0.1f;

    bool        logToDir      = true;
    std::string logDirPath    = "";
    std::string logFormat     = "csv";
    int         strfStationId = 0;

    bool        runOnLock     = true;
    std::string programPath   = "/usr/bin/notify-send";
    std::string programArgs   =
        "-e -a AmateurDSN \"%drifttool:name%\" \"Lock acquired on <b>%drifttool:name%</b> (carrier: %drifttool:freq% Hz)\"";



    // Overriden methods
    void deserialize(Suscan::Object const &conf) override;
    Suscan::Object &&serialize() override;
  };


  class DriftTool : public ToolWidget
  {
    Q_OBJECT

    Suscan::Analyzer  *m_analyzer    = nullptr;
    DriftToolConfig   *m_panelConfig = nullptr;
    DriftProcessor    *m_processor   = nullptr;
    MainSpectrum      *m_spectrum    = nullptr;
    DetachableProcess *m_process    = nullptr;

    // Log saver state
    QFile   m_logFile;
    QString m_logFileName;
    QString m_logFilePath;
    bool    m_loggingSTRF = false;
    bool    m_haveLog = false;


    // Global properties
    static bool g_propsCreated;
    GlobalProperty *m_propLock   = nullptr;
    GlobalProperty *m_propFreq   = nullptr;
    GlobalProperty *m_propRef    = nullptr;
    GlobalProperty *m_propName   = nullptr;
    GlobalProperty *m_propShift  = nullptr;
    GlobalProperty *m_propDrift  = nullptr;
    GlobalProperty *m_propVel    = nullptr;
    GlobalProperty *m_propAccel  = nullptr;

    // Named channels
    NamedChannelSetIterator m_namChan;
    bool m_haveNamChan = false;

    // Other UI state properties
    bool    m_haveFirstReading = false;

    void applySpectrumState();
    void connectAll();
    void refreshUi();
    void refreshNamedChannel();

    bool openLog();
    void logMeasurement(SUSCOUNT, qreal full, qreal rel);
    void closeLog();

    void refreshMeasurements();
    void logCurrentShift(SUSCOUNT count);
    void doAutoTrack(qreal chanRelShift);
    void notifyLock();

  public:
    explicit DriftTool(DriftToolFactory *, UIMediator *, QWidget *parent = nullptr);
    ~DriftTool() override;

    // Configuration methods
    Suscan::Serializable *allocConfig() override;
    void applyConfig() override;
    bool event(QEvent *) override;

    // Overriden methods
    void setState(int, Suscan::Analyzer *) override;
    void setQth(Suscan::Location const &) override;
    void setColorConfig(ColorConfig const &) override;
    void setTimeStamp(struct timeval const &) override;
    void setProfile(Suscan::Source::Config &) override;

  public slots:
    void onToggleOpenChannel();
    void onSpectrumFrequencyChanged(qint64);
    void onChannelStateChange(int, QString const &);
    void onMeasurement(quint64, qreal, qreal);
    void onLockStateChanged(bool);
    void onAdjust();
    void onRetuneChanged();
    void onToggleLog();
    void onToggleRun();
    void onNameChanged();
    void onChangeCutoff();
    void onChangeThreshold();

    void onBrowseLogDirectory();
    void onBrowseProgramPath();

    void onConfigChanged();

    void onProcessOpened();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int code, QProcess::ExitStatus status);

    void onPropNameChanged();
    void onPropRefChanged();

  private:
    Ui::DriftTool *ui;
  };

}

#endif // DRIFTTOOL_H
