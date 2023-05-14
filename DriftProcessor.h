//
//    DriftProcessor.h: description
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
#ifndef DRIFTPROCESSOR_H
#define DRIFTPROCESSOR_H

#include <QObject>
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

  enum DriftProcessorState {
    DRIFT_PROCESSOR_IDLE,         // Channel closed
    DRIFT_PROCESSOR_OPENING,      // Have request Id, open() sent
    DRIFT_PROCESSOR_CONFIGURING,  // Have inspector Id, set_params() sent
    DRIFT_PROCESSOR_STREAMING,    // set_params ack, starting sample delivery (hold)
  };

  class DriftProcessor : public QObject
  {
    Q_OBJECT

    Suscan::Analyzer   *m_analyzer = nullptr;
    Suscan::AnalyzerRequestTracker *m_tracker = nullptr;

    Suscan::Handle      m_inspHandle      = -1;
    uint32_t            m_inspId          = 0xffffffff;
    UIMediator         *m_mediator        = nullptr;
    suscan_config_t    *m_cfgTemplate     = nullptr;
    bool                m_inspectorOpened = false;
    DriftProcessorState m_state           = DRIFT_PROCESSOR_IDLE;
    bool                m_settingParams   = false;
    qreal               m_desiredFeedback = .1; // Desired feedback time (seconds)
    qreal               m_alpha;               // Used to average
    qreal               m_desiredBandwidth = 0;
    qreal               m_desiredFrequency = 0;
    qreal               m_desiredCutOff = 0;
    qreal               m_desiredThreshold = 0.25;
    unsigned int        m_fftSize = 8192;
    bool                m_lock = false;

    // These are only set if state > OPENING
    qreal               m_fullSampleRate;
    qreal               m_equivSampleRate;
    unsigned            m_decimation;
    qreal               m_maxBandwidth;
    qreal               m_chanRBW;

    // These are only set during streaming
    qreal               m_trueFeedback; /* After knowing the sample rate */
    qreal               m_trueBandwidth;
    SUSCOUNT            m_rawSampleCount = 0;
    SUSCOUNT            m_stabilGoal = 0;
    bool                m_stabilized = false;

    qreal               m_trueCutOff = 0;
    qreal               m_trueThreshold = 0;
    qreal               m_trueStabilization;

    // These are derived quantities
    qreal               m_prevSmoothShift = 0;
    qreal               m_currSmoothShift = 0;
    qreal               m_currSmoothDrift = 0;

    void configureInspector();
    qreal adjustBandwidth(qreal desired) const;
    void disconnectAnalyzer();
    void connectAnalyzer();
    void closeChannel();
    bool openChannel();
    void setState(DriftProcessorState, QString const &);
    void resetPLL();

    void connectAll();

  public:
    explicit DriftProcessor(UIMediator *, QObject *parent = nullptr);
    virtual ~DriftProcessor() override;

    DriftProcessorState state() const;
    void  setFFTSizeHint(unsigned int);
    void  setAnalyzer(Suscan::Analyzer *);

    bool  isRunning() const;
    bool  cancel();

    bool hasLock() const;

    void setFeedbackInterval(qreal);
    qreal setBandwidth(qreal);
    void  setFrequency(qreal);

    qreal getMinBandwidth() const;
    qreal getMaxBandwidth() const;
    qreal getTrueBandwidth() const;
    qreal getTrueFeedbackInterval() const;
    qreal getTrueThreshold() const;
    qreal getTrueCutOff() const;
    qreal getEquivFs() const;

    qreal getCurrShift() const;
    qreal getCurrDrift() const;
    bool isStable() const;

    unsigned getDecimation() const;

    bool  startStreaming(SUFREQ, SUFLOAT);

  public slots:
    void onInspectorMessage(Suscan::InspectorMessage const &);
    void onInspectorSamples(Suscan::SamplesMessage const &);
    void onOpened(Suscan::AnalyzerRequest const &);
    void onCancelled(Suscan::AnalyzerRequest const &);
    void onError(Suscan::AnalyzerRequest const &, std::string const &);

  signals:
    void stateChanged(int, QString const &);
    void measurement(qreal, qreal);
    void lockState(bool);
  };
}

#endif // DRIFTPROCESSOR_H
