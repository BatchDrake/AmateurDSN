//
//    DriftProcessor.cpp: description
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
#include "DriftProcessor.h"
#include <UIMediator.h>
#include <SuWidgetsHelpers.h>
#include <Suscan/AnalyzerRequestTracker.h>

using namespace SigDigger;

DriftProcessor::DriftProcessor(UIMediator *mediator, QObject *parent)
  : QObject{parent}
{
  m_mediator = mediator;
  m_tracker = new Suscan::AnalyzerRequestTracker(this);

  this->connectAll();

  this->setState(DRIFT_PROCESSOR_IDLE, "Idle");
}

DriftProcessor::~DriftProcessor()
{
  if (m_cfgTemplate != nullptr)
    suscan_config_destroy(m_cfgTemplate);
}

void
DriftProcessor::connectAll()
{
  connect(
        this->m_tracker,
        SIGNAL(opened(Suscan::AnalyzerRequest const &)),
        this,
        SLOT(onOpened(Suscan::AnalyzerRequest const &)));

  connect(
        this->m_tracker,
        SIGNAL(cancelled(Suscan::AnalyzerRequest const &)),
        this,
        SLOT(onCancelled(Suscan::AnalyzerRequest const &)));

  connect(
        this->m_tracker,
        SIGNAL(error(Suscan::AnalyzerRequest const &, const std::string &)),
        this,
        SLOT(onError(Suscan::AnalyzerRequest const &, const std::string &)));
}

qreal
DriftProcessor::adjustBandwidth(qreal desired) const
{
  if (m_decimation == 0)
    return desired;

  return m_chanRBW * ceil(desired / m_chanRBW);
}

void
DriftProcessor::disconnectAnalyzer()
{
  disconnect(m_analyzer, nullptr, this, nullptr);

  this->setState(DRIFT_PROCESSOR_IDLE, "Analyzer closed");
}

void
DriftProcessor::connectAnalyzer()
{
  connect(
        m_analyzer,
        SIGNAL(inspector_message(const Suscan::InspectorMessage &)),
        this,
        SLOT(onInspectorMessage(const Suscan::InspectorMessage &)));

  connect(
        m_analyzer,
        SIGNAL(samples_message(const Suscan::SamplesMessage &)),
        this,
        SLOT(onInspectorSamples(const Suscan::SamplesMessage &)));
}

void
DriftProcessor::closeChannel()
{
  if (m_analyzer != nullptr && m_inspHandle != -1)
    m_analyzer->closeInspector(m_inspHandle);

  m_inspHandle = -1;
}

// Depending on the state, a few things must be initialized
void
DriftProcessor::setState(DriftProcessorState state, QString const &msg)
{
  if (m_state != state) {
    m_state = state;

    switch (state) {
      case DRIFT_PROCESSOR_IDLE:
        if (m_inspHandle != -1)
          this->closeChannel();

        m_inspId = 0xffffffff;
        m_equivSampleRate = 0;
        m_fullSampleRate = 0;
        m_decimation = 0;
        m_chanRBW = 0;
        m_settingParams = false;
        break;

      case DRIFT_PROCESSOR_CONFIGURING:
        m_settingParams = true;
        break;

      case DRIFT_PROCESSOR_STREAMING:
        m_rawSampleCount  = 0;
        m_currSmoothDrift = 0;
        m_currSmoothShift = 0;
        m_prevSmoothShift = 0;
        m_lock            = false;
        m_stabilized      = false;
        break;

      default:
        break;
    }

    emit stateChanged(state, msg);
  }
}

void
DriftProcessor::resetPLL()
{
  Suscan::Config cfg(m_cfgTemplate);

  if (m_state == DRIFT_PROCESSOR_STREAMING) {
    cfg.set("drift.pll-reset", true);
    m_analyzer->setInspectorConfig(m_inspHandle, cfg);
  }
}

void
DriftProcessor::configureInspector()
{
  Suscan::Config cfg(m_cfgTemplate);

  cfg.set("drift.feedback-interval", SCAST(SUFLOAT, m_desiredFeedback));
  cfg.set("drift.lock-threshold", SCAST(SUFLOAT, m_desiredThreshold));
  m_analyzer->setInspectorConfig(m_inspHandle, cfg);

  this->setState(DRIFT_PROCESSOR_CONFIGURING, "Configuring params...");
}

bool
DriftProcessor::openChannel()
{
  Suscan::Channel ch;

  ch.bw    = m_desiredBandwidth;
  ch.fc    = m_desiredFrequency - m_analyzer->getFrequency();
  ch.fLow  = -.5 * m_desiredBandwidth;
  ch.fHigh = +.5 * m_desiredBandwidth;

  if (!m_tracker->requestOpen("drift", ch))
    return false;

  this->setState(DRIFT_PROCESSOR_OPENING, "Opening inspector...");

  return true;
}

///////////////////////////////// Public API //////////////////////////////////
DriftProcessorState
DriftProcessor::state() const
{
  return m_state;
}

void
DriftProcessor::setFFTSizeHint(unsigned int fftSize)
{
  m_fftSize = fftSize;
}

void
DriftProcessor::setAnalyzer(Suscan::Analyzer *analyzer)
{
  if (m_analyzer != nullptr)
    this->disconnectAnalyzer();

  m_analyzer = nullptr;
  if (analyzer == nullptr)
    this->setState(DRIFT_PROCESSOR_IDLE, "Capture stopped");
  else
    this->setState(DRIFT_PROCESSOR_IDLE, "Analyzer changed");

  m_analyzer = analyzer;

  if (m_analyzer != nullptr)
    connectAnalyzer();

  m_tracker->setAnalyzer(analyzer);
}

bool
DriftProcessor::isRunning() const
{
  return m_state != DRIFT_PROCESSOR_IDLE;
}

bool
DriftProcessor::cancel()
{
  if (isRunning()) {
    if (m_state == DRIFT_PROCESSOR_OPENING)
      m_tracker->cancelAll();
    this->setState(DRIFT_PROCESSOR_IDLE, "Cancelled by user");

    return true;
  }

  return false;
}

bool
DriftProcessor::hasLock() const
{
  return m_state == DRIFT_PROCESSOR_STREAMING && m_lock;
}

void
DriftProcessor::setFeedbackInterval(qreal desiredInterval)
{
  m_desiredFeedback = desiredInterval;

  if (m_state > DRIFT_PROCESSOR_OPENING)
    configureInspector();
}

qreal
DriftProcessor::getMaxBandwidth() const
{
  return m_maxBandwidth;
}

qreal
DriftProcessor::getMinBandwidth() const
{
  return m_chanRBW;
}

qreal
DriftProcessor::getTrueBandwidth() const
{
  return m_trueBandwidth;
}

qreal
DriftProcessor::getTrueFeedbackInterval() const
{
  return m_trueFeedback;
}

qreal
DriftProcessor::getTrueCutOff() const
{
  if (m_state == DRIFT_PROCESSOR_STREAMING)
    return m_trueCutOff;
  else
    return 0;
}

qreal
DriftProcessor::getTrueThreshold() const
{
  if (m_state == DRIFT_PROCESSOR_STREAMING)
    return m_trueThreshold;
  else
    return m_desiredThreshold;
}

qreal
DriftProcessor::setBandwidth(qreal desired)
{
  qreal ret;
  m_desiredBandwidth = desired;

  if (m_state > DRIFT_PROCESSOR_OPENING) {
    m_trueBandwidth = adjustBandwidth(m_desiredBandwidth);
    m_analyzer->setInspectorBandwidth(m_inspHandle, m_trueBandwidth);
    ret = m_trueBandwidth;
  } else {
    ret = desired;
  }

  return ret;
}

void
DriftProcessor::setFrequency(qreal fc)
{
  m_desiredFrequency = fc;
  if (m_state > DRIFT_PROCESSOR_OPENING) {
    m_analyzer->setInspectorFreq(
          m_inspHandle,
          m_desiredFrequency - m_analyzer->getFrequency());
  }
}

unsigned
DriftProcessor::getDecimation() const
{
  return m_decimation;
}

qreal
DriftProcessor::getEquivFs() const
{
  if (m_state > DRIFT_PROCESSOR_OPENING)
    return m_equivSampleRate;
  else
    return 0;
}

quint64
DriftProcessor::getSamplesPerUpdate() const
{
  return m_samplesPerUpdate;
}

qreal
DriftProcessor::getCurrShift() const
{
  if (hasLock())
    return m_currSmoothShift;
  else
    return 0;
}


qreal
DriftProcessor::getCurrDrift() const
{
  if (hasLock())
    return m_currSmoothDrift;
  else
    return 0;
}

bool
DriftProcessor::isStable() const
{
  return hasLock() && m_stabilized;
}

bool
DriftProcessor::startStreaming(SUFREQ fc, SUFLOAT bw)
{
  if (this->isRunning())
    return false;

  if (m_analyzer == nullptr)
    return false;

  this->setFrequency(fc);
  this->setBandwidth(SCAST(qreal, bw));

  return openChannel();
}

void
DriftProcessor::useConfigAsTemplate(const suscan_config_t *cfg)
{
  if (m_cfgTemplate != nullptr) {
    suscan_config_destroy(m_cfgTemplate);
    m_cfgTemplate = nullptr;
  }

  m_cfgTemplate = suscan_config_dup(cfg);
}

bool
DriftProcessor::setParamsFromConfig(const suscan_config_t *cfg)
{
  const struct suscan_field_value *cutoff, *threshold, *interval, *samps;

  useConfigAsTemplate(cfg);

  cutoff    = suscan_config_get_value(cfg, "drift.cutoff");
  threshold = suscan_config_get_value(cfg, "drift.lock-threshold");
  interval  = suscan_config_get_value(cfg, "drift.feedback-interval");
  samps     = suscan_config_get_value(cfg, "drift.feedback-samples");

  // Value is the same as requested? Go ahead
  if (cutoff != nullptr && threshold != nullptr && interval != nullptr) {
    m_trueCutOff       = SCAST(qreal, cutoff->as_float);
    m_trueThreshold    = SCAST(qreal, threshold->as_float);
    m_trueFeedback     = interval->as_float;
    m_samplesPerUpdate = samps->as_int;

    // Stabilization proportioinal to PLL cutoff
    m_trueStabilization = 30 / m_trueCutOff;

    // The goal is calculated in updates
    m_stabilGoal     = SCAST(SUSCOUNT, ceil(m_trueStabilization / m_trueFeedback));

    // Smoothing should happen at a speed proportional to the goal
    m_alpha          = SU_SPLPF_ALPHA(m_trueStabilization / m_trueFeedback);

    return true;
  }

  return false;
}

void
DriftProcessor::setCutOff(qreal cutoff)
{
  Suscan::Config cfg(m_cfgTemplate);

  if (m_state == DRIFT_PROCESSOR_STREAMING) {
    cfg.set("drift.cutoff", SU_ASFLOAT(cutoff));
    m_analyzer->setInspectorConfig(m_inspHandle, cfg);
  }
}

void
DriftProcessor::setThreshold(qreal threshold)
{
  Suscan::Config cfg(m_cfgTemplate);

  m_desiredThreshold = threshold;

  if (m_state == DRIFT_PROCESSOR_STREAMING) {
    cfg.set("drift.lock-threshold", SU_ASFLOAT(threshold));
    m_analyzer->setInspectorConfig(m_inspHandle, cfg);
  }
}

struct timeval
DriftProcessor::getLastLock() const
{
  return m_lastLock;
}

///////////////////////////// Analyzer slots //////////////////////////////////
void
DriftProcessor::onInspectorMessage(Suscan::InspectorMessage const &msg)
{
  bool configuring = m_state == DRIFT_PROCESSOR_CONFIGURING;

  if (msg.getInspectorId() == m_inspId) {
    // This refers to us!

    if (configuring) {
      switch (msg.getKind()) {
        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG:
          // Check if this is the acknowledgement of a "Setting rate" message
          // If this is the case, we transition to our final state
          if (m_settingParams) {
            m_settingParams = false;

            if (setParamsFromConfig(msg.getCConfig())) {
              this->setState(DRIFT_PROCESSOR_STREAMING, "Channel opened");
            } else {
              // This should never happen, but just in case the server is not
              // behaving as expected
              SU_ERROR("Some of the required parameters of the drift inspector were missing\n");
              cancel();
            }
          }

          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
          m_inspHandle = -1;
          this->setState(DRIFT_PROCESSOR_IDLE, "Inspector closed");
          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND:
        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT:
        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE:
          this->setState(DRIFT_PROCESSOR_IDLE, "Error during channel opening");
          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_TLE:
          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ORBIT_REPORT:
          break;

        default:
          break;
      }
    } else {
      switch (msg.getKind()) {
        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SIGNAL:
          if (msg.getSignalName() == "lock") {
            m_lock = msg.getSignalValue() > 0.;
            if (!m_lock) {
              m_rawSampleCount = 0;
              m_stabilized     = false;
            } else {
              m_lastLock       = m_analyzer->getSourceTimeStamp();
            }

            emit lockState(m_lock);
          }
          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG:
          setParamsFromConfig(m_cfgTemplate);
          break;

        default:
          break;
      }
    }
  }
}

void
DriftProcessor::onInspectorSamples(Suscan::SamplesMessage const &msg)
{
  SUSCOUNT sampCount = m_rawSampleCount;
  SUSCOUNT stabilizationGoal = m_stabilGoal;

  bool stable = m_stabilized;
  qreal carrier, channel;
  qreal currShift;
  qreal prevShift    = m_prevSmoothShift;
  bool reset = false;

  // Data delivered by the drift inspector is of the form:
  //
  // - Frequency of the carrier, relative to the channel center (in Hz)
  // - Frequency of the channel center, relative to the tuner (in Hz)
  //

  if (msg.getInspectorId() == m_inspId && hasLock()) {
    const SUCOMPLEX *samples = msg.getSamples();
    unsigned int count = msg.getCount();
    unsigned int i;
    for (i = 0; i < count; ++i) {
      carrier   = SCAST(qreal, SU_C_REAL(samples[i]));
      channel   = SCAST(qreal, SU_C_IMAG(samples[i]));
      currShift = carrier + channel;

      //
      // Locked to an alias, leave
      //
      if (!reset && fabs(carrier) > m_trueBandwidth) {
        resetPLL();
        reset = true;
      }

      // If we are stabilizing, do not put these noisy samples into the
      // smoothed variable

      prevShift = m_currSmoothShift;
      if (!stable) {
        m_currSmoothShift = currShift;
      } else {
        SU_SPLPF_FEED(m_currSmoothShift, currShift, m_alpha);
        SU_SPLPF_FEED(m_currSmoothDrift, (m_currSmoothShift - prevShift) / m_trueFeedback, m_alpha);
      }

      emit measurement(sampCount, carrier, channel);
      ++sampCount;

      if (!stable && sampCount >= stabilizationGoal)
        stable = true;
    }

    m_rawSampleCount  = sampCount;
    m_prevSmoothShift = prevShift;
    m_stabilized      = stable;
  }
}

////////////////////////////// Processor slots ////////////////////////////////
void
DriftProcessor::onOpened(Suscan::AnalyzerRequest const &req)
{
  // Async step 2: update state
  if (m_analyzer != nullptr) {
    // We do a lazy initialization of the audio channel parameters. Instead of
    // creating our own audio configuration template in the constructor, we
    // wait for the channel to provide the current configuration and
    // duplicate that one.

    useConfigAsTemplate(req.config);

    if (m_cfgTemplate == nullptr) {
      m_analyzer->closeInspector(req.handle);
      this->setState(DRIFT_PROCESSOR_IDLE, "Failed to duplicate configuration");
      return;
    }

    // Async step 3: set parameters
    m_inspHandle      = req.handle;
    m_inspId          = req.inspectorId;
    m_fullSampleRate  = SCAST(qreal, req.basebandRate);
    m_equivSampleRate = SCAST(qreal, req.equivRate);
    m_decimation      = SCAST(unsigned, m_fullSampleRate / m_equivSampleRate);

    m_maxBandwidth    = m_equivSampleRate;
    m_chanRBW         = m_fullSampleRate / m_fftSize;

    m_trueBandwidth   = adjustBandwidth(m_desiredBandwidth);

    // Adjust bandwidth to something that is physical and determined by the FFT
    m_analyzer->setInspectorBandwidth(m_inspHandle, m_trueBandwidth);
    // Enter in configuring state
    this->configureInspector();
  }
}

void
DriftProcessor::onCancelled(Suscan::AnalyzerRequest const &)
{
  this->setState(DRIFT_PROCESSOR_IDLE, "Cancelled");
}

void
DriftProcessor::onError(Suscan::AnalyzerRequest const &, std::string const &err)
{
  this->setState(
        DRIFT_PROCESSOR_IDLE,
        "Failed to open inspector: " + QString::fromStdString(err));
}
