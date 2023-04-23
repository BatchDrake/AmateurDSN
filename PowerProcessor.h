//
//    PowerProcessor.h: description
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
#ifndef POWERPROCESSOR_H
#define POWERPROCESSOR_H

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

  enum PowerProcessorState {
    POWER_PROCESSOR_IDLE,         // Channel closed
    POWER_PROCESSOR_OPENING,      // Have request Id, open() sent
    POWER_PROCESSOR_CONFIGURING,  // Have inspector Id, set_params() sent
    POWER_PROCESSOR_MEASURING,    // set_params ack, waiting for samples (one shot)
    POWER_PROCESSOR_STREAMING,    // set_params ack, starting sample delivery (hold)
  };

  class PowerProcessor : public QObject
  {
    Q_OBJECT

    Suscan::Analyzer   *m_analyzer = nullptr;
    Suscan::AnalyzerRequestTracker *m_tracker = nullptr;

    Suscan::Handle      m_inspHandle  = -1;
    uint32_t            m_inspId      = 0xffffffff;
    UIMediator         *m_mediator    = nullptr;
    suscan_config_t    *m_cfgTemplate = nullptr;
    bool                m_inspectorOpened = false;
    PowerProcessorState m_state       = POWER_PROCESSOR_IDLE;
    bool                m_settingRate = false;
    bool                m_oneShot = false; // Must remain the same until IDLE
    qreal               m_desiredTau = 1; // Desired integration time (seconds)
    qreal               m_desiredFeedback = 0.1; // Desired feedback time (seconds)
    qreal               m_alpha;
    qreal               m_desiredBandwidth = 0;
    qreal               m_desiredFrequency = 0;

    unsigned int        m_fftSize = 8192;

    // These are only set if state > OPENING
    qreal               m_fullSampleRate;
    qreal               m_equivSampleRate;
    unsigned            m_decimation;
    qreal               m_maxBandwidth;
    qreal               m_chanRBW;

    // These are only set during streaming
    quint64             m_inspIntSamples = 0;
    qreal               m_trueFeedback; /* After knowing the sample rate */
    qreal               m_trueTau;      /* After knowing the feedback rate */
    qreal               m_trueBandwidth;
    SUSCOUNT            m_rawSampleCount = 0;
    qreal               m_lastMeasurement = 0;

    void configureInspector();
    qreal adjustBandwidth(qreal desired) const;
    void disconnectAnalyzer();
    void connectAnalyzer();
    void closeChannel();
    bool openChannel();
    void setState(PowerProcessorState, QString const &);

    void connectAll();

  public:
    explicit PowerProcessor(UIMediator *, QObject *parent = nullptr);
    virtual ~PowerProcessor() override;

    PowerProcessorState state() const;
    void  setFFTSizeHint(unsigned int);
    void  setAnalyzer(Suscan::Analyzer *);

    bool  isRunning() const;
    bool  cancel();

    void setTau(qreal);
    void setFeedbackInterval(qreal);
    qreal setBandwidth(qreal);
    void  setFrequency(qreal);

    qreal getMinBandwidth() const;
    qreal getMaxBandwidth() const;
    qreal getTrueBandwidth() const;
    qreal getTrueFeedbackInterval() const;
    qreal getTrueTau() const;
    qreal getEquivFs() const;
    unsigned getDecimation() const;
    unsigned getIntSamples() const;

    bool  oneShot(SUFREQ, SUFLOAT);
    bool  startStreaming(SUFREQ, SUFLOAT);

  public slots:
    void onInspectorMessage(Suscan::InspectorMessage const &);
    void onInspectorSamples(Suscan::SamplesMessage const &);
    void onOpened(Suscan::AnalyzerRequest const &);
    void onCancelled(Suscan::AnalyzerRequest const &);
    void onError(Suscan::AnalyzerRequest const &, std::string const &);

  signals:
    void stateChanged(int, QString const &);
    void measurement(qreal);
  };
}

#endif // POWERPROCESSOR_H
