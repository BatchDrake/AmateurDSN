//
//    ProcessForwarder.h: description
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
#ifndef PROCESSFORWARDER_H
#define PROCESSFORWARDER_H

#include <QObject>
#include <QProcess>
#include <Suscan/Library.h>
#include <Suscan/Analyzer.h>
#include <AudioFileSaver.h>

namespace Suscan {
  class Analyzer;
  class AnalyzerRequestTracker;
  struct AnalyzerRequest;
};

namespace SigDigger {
  class UIMediator;
  class AudioPlayback;
  class DetachableProcess;

  class DetachableProcess : public QProcess
  {
  public:
    DetachableProcess(QObject *parent = nullptr);
    ~DetachableProcess() override;

    void detach();
  };

  enum ProcessForwarderState {
    PROCESS_FORWARDER_IDLE,         // Channel closed
    PROCESS_FORWARDER_OPENING,      // Have request Id, open() sent
    PROCESS_FORWARDER_LAUNCHING,     // Have inspector Id, set_params() sent
    PROCESS_FORWARDER_RUNNING,      // set_params ack, starting sample delivery (hold)
  };

  class ProcessForwarder : public QObject
  {
    Q_OBJECT

    Suscan::Analyzer   *m_analyzer = nullptr;
    Suscan::AnalyzerRequestTracker *m_tracker = nullptr;

    Suscan::Handle      m_inspHandle  = -1;
    uint32_t            m_inspId      = 0xffffffff;
    UIMediator         *m_mediator    = nullptr;
    bool                m_inspectorOpened = false;
    ProcessForwarderState m_state       = PROCESS_FORWARDER_IDLE;
    QString             m_programPath;
    QStringList         m_programArgs;
    qreal               m_desiredBandwidth = 0;
    qreal               m_desiredFrequency = 0;
    DetachableProcess   m_process;

    // These are only set if state > OPENING
    qreal               m_fullSampleRate;
    qreal               m_equivSampleRate;
    unsigned            m_decimation;
    qreal               m_maxBandwidth;
    qreal               m_chanRBW;
    unsigned int        m_fftSize = 8192;

    // These are only set during streaming
    qreal               m_trueBandwidth;

    qreal adjustBandwidth(qreal desired) const;
    void disconnectAnalyzer();
    void connectAnalyzer();
    void closeChannel();
    bool openChannel();
    void setState(ProcessForwarderState, QString const &);

    void connectAll();


  public:
    explicit ProcessForwarder(UIMediator *, QObject *parent = nullptr);
    virtual ~ProcessForwarder() override;

    ProcessForwarderState state() const;
    void  setAnalyzer(Suscan::Analyzer *);
    void  setFFTSizeHint(unsigned int);

    bool  run(QString const &, QStringList const &, SUFREQ, SUFLOAT);
    bool  isRunning() const;
    bool  cancel();
    bool  detach();

    qreal setBandwidth(qreal);
    void  setFrequency(qreal);

    qreal getMinBandwidth() const;
    qreal getMaxBandwidth() const;
    qreal getTrueBandwidth() const;
    qreal getEquivFs() const;
    unsigned getDecimation() const;

  public slots:
    void onInspectorMessage(Suscan::InspectorMessage const &);
    void onInspectorSamples(Suscan::SamplesMessage const &);
    void onOpened(Suscan::AnalyzerRequest const &);
    void onCancelled(Suscan::AnalyzerRequest const &);
    void onError(Suscan::AnalyzerRequest const &, std::string const &);

    void onProcessError(QProcess::ProcessError);
    void onProcessFinished(int, QProcess::ExitStatus);
    void onProcessStarted();

  signals:
    void stateChanged(int, QString const &);
  };
}

#endif // PROCESSFORWARDER_H
