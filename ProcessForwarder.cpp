//
//    ProcessForwarder.cpp: description
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
#include "ProcessForwarder.h"
#include <UIMediator.h>
#include <SuWidgetsHelpers.h>
#include <Suscan/AnalyzerRequestTracker.h>

using namespace SigDigger;

//////////////////////////////// Detachable process ////////////////////////////
DetachableProcess::DetachableProcess(QObject *parent) : QProcess(parent)
{

}

DetachableProcess::~DetachableProcess()
{

}

void
DetachableProcess::detach()
{
  this->waitForStarted();
  setProcessState(QProcess::NotRunning);
}

//////////////////////////////// ProcessForwarder ////////////////////////////
ProcessForwarder::ProcessForwarder(UIMediator *mediator, QObject *parent)
  : QObject{parent}
{
  m_mediator = mediator;
  m_tracker = new Suscan::AnalyzerRequestTracker(this);

  this->connectAll();

  this->setState(PROCESS_FORWARDER_IDLE, "Idle");
}

ProcessForwarder::~ProcessForwarder()
{
}

void
ProcessForwarder::connectAll()
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

  connect(
        &this->m_process,
        SIGNAL(started()),
        this,
        SLOT(onProcessStarted()));

  connect(
        &this->m_process,
        SIGNAL(errorOccurred(QProcess::ProcessError)),
        this,
        SLOT(onProcessError(QProcess::ProcessError)));

  connect(
        &this->m_process,
        SIGNAL(finished(int,QProcess::ExitStatus)),
        this,
        SLOT(onProcessFinished(int,QProcess::ExitStatus)));
}

qreal
ProcessForwarder::adjustBandwidth(qreal desired) const
{
  if (m_decimation == 0)
    return desired;

  return m_chanRBW * ceil(desired / m_chanRBW);
}

void
ProcessForwarder::disconnectAnalyzer()
{
  disconnect(m_analyzer, nullptr, this, nullptr);

  this->setState(PROCESS_FORWARDER_IDLE, "Analyzer closed");
}

void
ProcessForwarder::connectAnalyzer()
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
ProcessForwarder::closeChannel()
{
  if (m_analyzer != nullptr && m_inspHandle != -1)
    m_analyzer->closeInspector(m_inspHandle);

  m_inspHandle = -1;
}

void
ProcessForwarder::setFFTSizeHint(unsigned int fftSize)
{
  m_fftSize = fftSize;
}


// Depending on the state, a few things must be initialized
void
ProcessForwarder::setState(ProcessForwarderState state, QString const &msg)
{
  QStringList correctedList;

  if (m_state != state) {
    m_state = state;

    switch (state) {
      case PROCESS_FORWARDER_IDLE:
        if (m_inspHandle != -1)
          this->closeChannel();

        m_inspId = 0xffffffff;
        m_equivSampleRate = 0;
        m_fullSampleRate = 0;
        m_decimation = 0;
        m_chanRBW = 0;

        if (m_process.state() != QProcess::NotRunning) {
          m_process.waitForStarted(1000);
          m_process.terminate();
        }

        break;

      case PROCESS_FORWARDER_LAUNCHING:
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.setInputChannelMode(QProcess::ManagedInputChannel);
        m_process.setProgram(m_programPath);

        for (auto p : m_programArgs) {
          QString arg = p;
          arg = arg.replace(
            "%SAMPLERATE%",
            QString::number(SCAST(int, m_equivSampleRate)));
          arg = arg.replace(
            "%FFTSIZE%",
            QString::number(SCAST(int, m_fftSize)));
          correctedList.append(arg);
        }

        m_process.setArguments(correctedList);
        m_process.start();

        break;

      case PROCESS_FORWARDER_RUNNING:
        break;

      default:
        break;
    }

    emit stateChanged(state, msg);
  }
}

bool
ProcessForwarder::openChannel()
{
  Suscan::Channel ch;

  ch.bw    = m_desiredBandwidth;
  ch.fc    = m_desiredFrequency - m_analyzer->getFrequency();
  ch.fLow  = -.5 * m_desiredBandwidth;
  ch.fHigh = +.5 * m_desiredBandwidth;

  if (!m_tracker->requestOpen("raw", ch))
    return false;

  this->setState(PROCESS_FORWARDER_OPENING, "Opening inspector...");

  return true;
}

///////////////////////////////// Public API //////////////////////////////////
ProcessForwarderState
ProcessForwarder::state() const
{
  return m_state;
}

void
ProcessForwarder::setAnalyzer(Suscan::Analyzer *analyzer)
{
  if (m_analyzer != nullptr)
    this->disconnectAnalyzer();

  m_analyzer = nullptr;
  if (analyzer == nullptr)
    this->setState(PROCESS_FORWARDER_IDLE, "Capture stopped");
  else
    this->setState(PROCESS_FORWARDER_IDLE, "Analyzer changed");

  m_analyzer = analyzer;

  if (m_analyzer != nullptr)
    connectAnalyzer();

  m_tracker->setAnalyzer(analyzer);
}

bool
ProcessForwarder::isRunning() const
{
  return m_state != PROCESS_FORWARDER_IDLE;
}

bool
ProcessForwarder::cancel()
{
  if (isRunning()) {
    if (m_state == PROCESS_FORWARDER_OPENING)
      m_tracker->cancelAll();
    this->setState(PROCESS_FORWARDER_IDLE, "Cancelled by user");

    return true;
  }

  return false;
}

bool
ProcessForwarder::detach()
{
  if (isRunning()) {
    m_process.detach();
    m_tracker->cancelAll();
    this->setState(PROCESS_FORWARDER_IDLE, "Process detached");
    return true;
  }

  return false;
}

qreal
ProcessForwarder::getMaxBandwidth() const
{
  return m_maxBandwidth;
}

qreal
ProcessForwarder::getMinBandwidth() const
{
  return m_chanRBW;
}

qreal
ProcessForwarder::getTrueBandwidth() const
{
  return m_trueBandwidth;
}

qreal
ProcessForwarder::setBandwidth(qreal desired)
{
  qreal ret;
  m_desiredBandwidth = desired;

  if (m_state > PROCESS_FORWARDER_OPENING) {
    m_trueBandwidth = adjustBandwidth(m_desiredBandwidth);
    m_analyzer->setInspectorBandwidth(m_inspHandle, m_trueBandwidth);
    ret = m_trueBandwidth;
  } else {
    ret = desired;
  }

  return ret;
}

void
ProcessForwarder::setFrequency(qreal fc)
{
  m_desiredFrequency = fc;
  if (m_state > PROCESS_FORWARDER_OPENING) {
    m_analyzer->setInspectorFreq(m_inspHandle, m_desiredFrequency - m_analyzer->getFrequency());
  }
}

unsigned
ProcessForwarder::getDecimation() const
{
  return m_decimation;
}

qreal
ProcessForwarder::getEquivFs() const
{
  if (m_state > PROCESS_FORWARDER_OPENING)
    return m_equivSampleRate;
  else
    return 0;
}

bool
ProcessForwarder::run(
    QString const &prog,
    QStringList const &args,
    SUFREQ fc,
    SUFLOAT bw)
{
  if (this->isRunning())
    return false;

  m_programPath = prog;
  m_programArgs = args;

  this->setFrequency(fc);
  this->setBandwidth(SCAST(qreal, bw));

  return openChannel();
}

///////////////////////////// Analyzer slots //////////////////////////////////
void
ProcessForwarder::onInspectorMessage(Suscan::InspectorMessage const &msg)
{
  if (msg.getInspectorId() == m_inspId) {
    // This refers to us!

    switch (msg.getKind()) {
      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
        m_inspHandle = -1;
        this->setState(PROCESS_FORWARDER_IDLE, "Inspector closed");
        break;

      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND:
      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_OBJECT:
      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE:
        this->setState(PROCESS_FORWARDER_IDLE, "Error during channel opening");
        break;

      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_SET_TLE:
        break;

      case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_ORBIT_REPORT:
        break;

      default:
        break;
    }
  }
}

void
ProcessForwarder::onInspectorSamples(Suscan::SamplesMessage const &msg)
{
  // Feed samples, only if the sample rate is right
  if (msg.getInspectorId() == m_inspId) {
    const SUCOMPLEX *samples = msg.getSamples();
    unsigned int count = msg.getCount();

    if (m_state == PROCESS_FORWARDER_RUNNING
        && m_process.state() == QProcess::Running)
      m_process.write(
            reinterpret_cast<const char *>(samples),
            count * sizeof(SUCOMPLEX));
  }
}

////////////////////////////// Processor slots ////////////////////////////////
void
ProcessForwarder::onOpened(Suscan::AnalyzerRequest const &req)
{
  // Async step 2: update state
  if (m_analyzer != nullptr) {
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


    // We now transition to LAUNCHING and wait for the process initialization
    this->setState(PROCESS_FORWARDER_LAUNCHING, "Launching program...");
  }
}

void
ProcessForwarder::onCancelled(Suscan::AnalyzerRequest const &)
{
  this->setState(PROCESS_FORWARDER_IDLE, "Cancelled");
}

void
ProcessForwarder::onError(Suscan::AnalyzerRequest const &, std::string const &err)
{
  this->setState(
        PROCESS_FORWARDER_IDLE,
        "Failed to open inspector: " + QString::fromStdString(err));
}

void
ProcessForwarder::onProcessError(QProcess::ProcessError error)
{
  QString reason;

  switch (error) {
    case QProcess::ProcessError::ReadError:
      reason = "Read error";
      break;

    case QProcess::ProcessError::FailedToStart:
      reason = "Process failed to start";
      break;

    case QProcess::ProcessError::Crashed:
      reason = "Process crashed";
      break;

    case QProcess::ProcessError::Timedout:
      reason = "Process took too long to start";
      break;

    case QProcess::ProcessError::WriteError:
      reason = "Write error";
      break;

    case QProcess::ProcessError::UnknownError:
      reason = "Unknown reason";
  }

  this->setState(PROCESS_FORWARDER_IDLE, reason);
}

void
ProcessForwarder::onProcessFinished(int code, QProcess::ExitStatus status)
{
  QString reason;

  if (status == QProcess::CrashExit)
    reason = "Child process crashed";
  else if (code != 0)
    reason = "Process finished (error " + QString::number(code) + ")";
  else
    reason = "Process finished normally";

  this->setState(PROCESS_FORWARDER_IDLE, reason);
}

void
ProcessForwarder::onProcessStarted()
{
  this->setState(
        PROCESS_FORWARDER_RUNNING,
        "Running at "
        + SuWidgetsHelpers::formatQuantity(m_equivSampleRate, 3, "sps"));
}
