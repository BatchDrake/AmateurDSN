//
//    PowerProcessor.cpp: description
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
#include "PowerProcessor.h"
#include <UIMediator.h>
#include <SuWidgetsHelpers.h>
#include <Suscan/AnalyzerRequestTracker.h>

using namespace SigDigger;

PowerProcessor::PowerProcessor(UIMediator *mediator, QObject *parent)
  : QObject{parent}
{
  m_mediator = mediator;
  m_tracker = new Suscan::AnalyzerRequestTracker(this);

  this->connectAll();

  this->setState(POWER_PROCESSOR_IDLE, "Idle");
}

PowerProcessor::~PowerProcessor()
{
  if (m_cfgTemplate != nullptr)
    suscan_config_destroy(m_cfgTemplate);
}

void
PowerProcessor::connectAll()
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
PowerProcessor::adjustBandwidth(qreal desired) const
{
  if (m_decimation == 0)
    return desired;

  return m_chanRBW * ceil(desired / m_chanRBW);
}

void
PowerProcessor::disconnectAnalyzer()
{
  disconnect(m_analyzer, nullptr, this, nullptr);

  this->setState(POWER_PROCESSOR_IDLE, "Analyzer closed");
}

void
PowerProcessor::connectAnalyzer()
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
PowerProcessor::closeChannel()
{
  if (m_analyzer != nullptr && m_inspHandle != -1)
    m_analyzer->closeInspector(m_inspHandle);

  m_inspHandle = -1;
}

// Depending on the state, a few things must be initialized
void
PowerProcessor::setState(PowerProcessorState state, QString const &msg)
{
  if (m_state != state) {
    m_state = state;

    m_haveBpe = false;

    switch (state) {
      case POWER_PROCESSOR_IDLE:
        if (m_inspHandle != -1)
          this->closeChannel();

        m_inspId = 0xffffffff;
        m_inspIntSamples = 0;
        m_equivSampleRate = 0;
        m_fullSampleRate = 0;
        m_decimation = 0;
        m_chanRBW = 0;
        m_settingRate = false;
        break;

      case POWER_PROCESSOR_CONFIGURING:
        m_settingRate = true;
        break;

      case POWER_PROCESSOR_MEASURING:
      case POWER_PROCESSOR_STREAMING:
        m_rawSampleCount = 0;
        m_lastMeasurement = 0;
        suscan_bpe_init(&m_bpe);
        m_haveBpe = true;

        break;

      default:
        break;
    }

    emit stateChanged(state, msg);
  }
}

void
PowerProcessor::configureInspector()
{
  unsigned samples;
  Suscan::Config cfg(m_cfgTemplate);

  if (m_oneShot) {
    samples           = SCAST(unsigned, ceil(m_desiredTau * m_equivSampleRate));
    m_trueTau         = samples / m_equivSampleRate;
    m_trueFeedback    = m_trueTau;
  } else {
    samples           = SCAST(unsigned, ceil(m_desiredFeedback * m_equivSampleRate));
    m_trueFeedback    = samples / m_equivSampleRate;
    m_alpha           = SCAST(qreal, SU_SPLPF_ALPHA(SU_ASFLOAT(m_desiredTau / m_trueFeedback)));
    m_kInt            = SCAST(SUSCOUNT, (2. - m_alpha) / m_alpha);
    m_trueTau         = m_desiredTau;
    m_rawSampleCount  = 0;
  }

  m_inspIntSamples = samples;

  cfg.set("power.integrate-samples", SCAST(uint64_t, m_inspIntSamples));

  // Now we have no scaling
  m_haveScaling = false;
  m_analyzer->setInspectorConfig(m_inspHandle, cfg);

  this->setState(POWER_PROCESSOR_CONFIGURING, "Configuring params...");
}

bool
PowerProcessor::openChannel()
{
  Suscan::Channel ch;

  ch.bw    = m_desiredBandwidth;
  ch.fc    = m_desiredFrequency - m_analyzer->getFrequency();
  ch.fLow  = -.5 * m_desiredBandwidth;
  ch.fHigh = +.5 * m_desiredBandwidth;

  if (!m_tracker->requestOpen("power", ch))
    return false;

  this->setState(POWER_PROCESSOR_OPENING, "Opening inspector...");

  return true;
}

///////////////////////////////// Public API //////////////////////////////////
PowerProcessorState
PowerProcessor::state() const
{
  return m_state;
}

void
PowerProcessor::setFFTSizeHint(unsigned int fftSize)
{
  m_fftSize = fftSize;
}

void
PowerProcessor::setAnalyzer(Suscan::Analyzer *analyzer)
{
  if (m_analyzer != nullptr)
    this->disconnectAnalyzer();

  m_analyzer = nullptr;
  if (analyzer == nullptr)
    this->setState(POWER_PROCESSOR_IDLE, "Capture stopped");
  else
    this->setState(POWER_PROCESSOR_IDLE, "Analyzer changed");

  m_analyzer = analyzer;

  if (m_analyzer != nullptr)
    connectAnalyzer();

  m_tracker->setAnalyzer(analyzer);
}

bool
PowerProcessor::isRunning() const
{
  return m_state != POWER_PROCESSOR_IDLE;
}

bool
PowerProcessor::cancel()
{
  if (isRunning()) {
    if (m_state == POWER_PROCESSOR_OPENING)
      m_tracker->cancelAll();
    this->setState(POWER_PROCESSOR_IDLE, "Cancelled by user");

    return true;
  }

  return false;
}

void
PowerProcessor::setTau(qreal desiredTau)
{
  m_desiredTau = desiredTau;

  if (m_state > POWER_PROCESSOR_OPENING)
    configureInspector();
}

void
PowerProcessor::setFeedbackInterval(qreal desiredInterval)
{
  m_desiredFeedback = desiredInterval;

  if (m_state > POWER_PROCESSOR_OPENING)
    configureInspector();
}

qreal
PowerProcessor::getMaxBandwidth() const
{
  return m_maxBandwidth;
}

qreal
PowerProcessor::getMinBandwidth() const
{
  return m_chanRBW;
}

qreal
PowerProcessor::getTrueBandwidth() const
{
  return m_trueBandwidth;
}

qreal
PowerProcessor::getTrueFeedbackInterval() const
{
  return m_trueFeedback;
}

qreal
PowerProcessor::setBandwidth(qreal desired)
{
  qreal ret;
  m_desiredBandwidth = desired;

  if (m_state > POWER_PROCESSOR_OPENING) {
    m_trueBandwidth = adjustBandwidth(m_desiredBandwidth);
    m_analyzer->setInspectorBandwidth(m_inspHandle, m_trueBandwidth);
    ret = m_trueBandwidth;
  } else {
    ret = desired;
  }

  return ret;
}

void
PowerProcessor::setFrequency(qreal fc)
{
  m_desiredFrequency = fc;
  if (m_state > POWER_PROCESSOR_OPENING) {
    m_analyzer->setInspectorFreq(m_inspHandle, m_desiredFrequency - m_analyzer->getFrequency());
  }
}

unsigned
PowerProcessor::getDecimation() const
{
  return m_decimation;
}

unsigned
PowerProcessor::getIntSamples() const
{
  return m_inspIntSamples;
}

qreal
PowerProcessor::getTrueTau() const
{
  if (m_state > POWER_PROCESSOR_OPENING)
    return m_trueTau;
  else
    return m_desiredTau;
}

qreal
PowerProcessor::getEquivFs() const
{
  if (m_state > POWER_PROCESSOR_OPENING)
    return m_equivSampleRate;
  else
    return 0;
}


bool
PowerProcessor::oneShot(SUFREQ fc, SUFLOAT bw)
{
  if (this->isRunning())
    return false;

  if (m_analyzer == nullptr)
    return false;

  this->setFrequency(fc);
  this->setBandwidth(SCAST(qreal, bw));
  this->m_oneShot = true;

  return openChannel();
}

bool
PowerProcessor::haveBpe() const
{
  return m_haveBpe && m_haveScaling && m_rawSampleCount > 0;
}

void
PowerProcessor::resetBpe()
{
  suscan_bpe_init(&m_bpe);
}

qreal
PowerProcessor::powerModeBpe()
{
  return suscan_bpe_get_power(&m_bpe);
}

qreal
PowerProcessor::powerDeltaBpe()
{
  return suscan_bpe_get_dispersion(&m_bpe);
}

bool
PowerProcessor::startStreaming(SUFREQ fc, SUFLOAT bw)
{
  if (this->isRunning())
    return false;

  if (m_analyzer == nullptr)
    return false;

  this->setFrequency(fc);
  this->setBandwidth(SCAST(qreal, bw));
  this->m_oneShot = false;

  return openChannel();
}

///////////////////////////// Analyzer slots //////////////////////////////////
void
PowerProcessor::onInspectorMessage(Suscan::InspectorMessage const &msg)
{
  bool configuring = m_state == POWER_PROCESSOR_CONFIGURING;

  if (msg.getInspectorId() == m_inspId) {
    // This refers to us!
    if (configuring) {
      switch (msg.getKind()) {
        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_CONFIG:
          // Check if this is the acknowledgement of a "Setting rate" message
          // If this is the case, we transition to our final state
          if (m_settingRate) {
            const suscan_config_t *cfg = msg.getCConfig();
            const struct suscan_field_value *value;

            value = suscan_config_get_value(cfg, "power.integrate-samples");

            // Value is the same as requested? Go ahead
            if (value != nullptr) {
              if (m_inspIntSamples == value->as_int) {
                m_settingRate = false;

                if (m_oneShot) {
                  this->setState(POWER_PROCESSOR_MEASURING, "Measuring power...");
                } else {
                  this->setState(POWER_PROCESSOR_STREAMING, "Channel opened");
                }
              }
            } else {
              // This should never happen, but just in case the server is not
              // behaving as expected
              m_settingRate = false;
            }
          }

          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
          m_inspHandle = -1;
          this->setState(POWER_PROCESSOR_IDLE, "Inspector closed");
          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND:
        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT:
        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE:
          this->setState(POWER_PROCESSOR_IDLE, "Error during channel opening");
          break;


        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_TLE:
          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ORBIT_REPORT:
          break;

        default:
          break;
      }
    }

    if (msg.getKind() == SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SIGNAL) {
      auto name = msg.getSignalName();

      if (msg.getSignalName() == "scaling") {
        m_haveScaling = true;
        m_bpeScaling  = msg.getSignalValue();
      } else if (msg.getSignalName() == "insp.true_bw") {
        m_trueBandwidth = msg.getSignalValue();
      }
    }
  }
}

void
PowerProcessor::onInspectorSamples(Suscan::SamplesMessage const &msg)
{
  // Feed samples, only if the sample rate is right
  if (msg.getInspectorId() == m_inspId) {
    const SUCOMPLEX *samples = msg.getSamples();
    unsigned int count = msg.getCount();
    unsigned int i;

    if (m_state == POWER_PROCESSOR_MEASURING) {
      m_lastMeasurement = SCAST(qreal, SU_C_REAL(samples[count - 1]));
      emit measurement(m_lastMeasurement);
      this->setState(POWER_PROCESSOR_IDLE, "Done");
    } else if (m_state == POWER_PROCESSOR_STREAMING) {
      SUSCOUNT sampCount = m_rawSampleCount;
      qreal    power, lastMeasurement;
      lastMeasurement = m_lastMeasurement;

      for (i = 0; i < count; ++i) {
        power =  SCAST(qreal, SU_C_REAL(samples[i]));

        if (sampCount == 0)
          lastMeasurement = power;
        else
          SU_SPLPF_FEED(lastMeasurement, power, m_alpha);

        if (m_haveBpe && m_haveScaling && sampCount > 0)
          suscan_bpe_feed(&m_bpe, power, m_bpeScaling);

        emit measurement(lastMeasurement);

        ++sampCount;
      }

      m_rawSampleCount  = sampCount;
      m_lastMeasurement = lastMeasurement;
    }
  }
}

////////////////////////////// Processor slots ////////////////////////////////
void
PowerProcessor::onOpened(Suscan::AnalyzerRequest const &req)
{
  // Async step 2: update state
  if (m_analyzer != nullptr) {
    // We do a lazy initialization of the audio channel parameters. Instead of
    // creating our own audio configuration template in the constructor, we
    // wait for the channel to provide the current configuration and
    // duplicate that one.

    if (m_cfgTemplate != nullptr) {
      suscan_config_destroy(m_cfgTemplate);
      m_cfgTemplate = nullptr;
    }

    m_cfgTemplate = suscan_config_dup(req.config);

    if (m_cfgTemplate == nullptr) {
      m_analyzer->closeInspector(req.handle);
      this->setState(POWER_PROCESSOR_IDLE, "Failed to duplicate configuration");
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
    // This will trigger the receiption of an insp.true_bw signal
    m_analyzer->setInspectorBandwidth(m_inspHandle, m_trueBandwidth);

    // Enter in configuring state
    this->configureInspector();
  }
}

void
PowerProcessor::onCancelled(Suscan::AnalyzerRequest const &)
{
  this->setState(POWER_PROCESSOR_IDLE, "Cancelled");
}

void
PowerProcessor::onError(Suscan::AnalyzerRequest const &, std::string const &err)
{
  this->setState(
        POWER_PROCESSOR_IDLE,
        "Failed to open inspector: " + QString::fromStdString(err));
}
