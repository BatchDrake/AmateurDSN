//
//    DriftTool.cpp: description
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
#include <MainSpectrum.h>
#include <QFileDialog>
#include <QDir>
#include <GlobalProperty.h>
#include <QTextStream>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QRegularExpression>
#  define QRegExp QRegularExpression
#else
#  include <QRegExp>
#endif

#include "DriftTool.h"
#include "DriftProcessor.h"
#include "AmateurDSNHelpers.h"
#include "DetachableProcess.h"

#include "ui_DriftTool.h"

#define STRINGFY(x) #x
#define STORE(field) obj.set(STRINGFY(field), this->field)
#define LOAD(field) this->field = conf.get(STRINGFY(field), this->field)

#define BLOCKSIG_BEGIN(object)                   \
  do {                                           \
    QObject *obj = object;                       \
    bool blocked = (object)->blockSignals(true)

#define BLOCKSIG_END()                           \
    obj->blockSignals(blocked);                  \
  } while (false)

#define BLOCKSIG(object, op)                     \
  do {                                           \
    bool blocked = (object)->blockSignals(true); \
    (object)->op;                                \
    (object)->blockSignals(blocked);             \
  } while (false)


using namespace SigDigger;

bool DriftTool::g_propsCreated = false;

//////////////////////////// Widget config /////////////////////////////////////
void
DriftToolConfig::deserialize(Suscan::Object const &conf)
{
  LOAD(collapsed);
  LOAD(reference);
  LOAD(lockThres);
  LOAD(retune);
  LOAD(retuneTrigger);
  LOAD(logToDir);
  LOAD(logDirPath);
  LOAD(runOnLock);
  LOAD(programPath);
  LOAD(programArgs);
}

Suscan::Object &&
DriftToolConfig::serialize()
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("DriftToolConfig");

  STORE(collapsed);
  STORE(reference);
  STORE(lockThres);
  STORE(retune);
  STORE(retuneTrigger);
  STORE(logToDir);
  STORE(logDirPath);
  STORE(runOnLock);
  STORE(programPath);
  STORE(programArgs);

  return persist(obj);
}

/////////////////////////// Widget implementation //////////////////////////////
DriftTool::DriftTool(
    DriftToolFactory *factory,
    UIMediator *mediator,
    QWidget *parent) :
  ToolWidget(factory, mediator, parent),
  ui(new Ui::DriftTool)
{
  ui->setupUi(this);

  assertConfig();

  m_processor = new DriftProcessor(mediator, this);
  m_mediator  = mediator;
  m_spectrum  = mediator->getMainSpectrum();

  ui->pllBwSpin->setAutoUnitMultiplierEnabled(true);
  ui->pllBwSpin->setSubMultiplesAllowed(true);

  setProperty("collapsed", m_panelConfig->collapsed);

  if (!g_propsCreated) {
    m_propLock = GlobalProperty::registerProperty(
          "drifttool:lock",
          "Drift Tool: lock status [unlocked|searching|locked|stable|tracking]",
          QString("UNLOCKED"));

    m_propFreq = GlobalProperty::registerProperty(
          "drifttool:freq",
          "Drift Tool: carrier frequency [Hz]",
          0.);

    m_propRef = GlobalProperty::registerProperty(
          "drifttool:ref",
          "Drift Tool: reference frequency [Hz]",
          0.);

    m_propName = GlobalProperty::registerProperty(
          "drifttool:name",
          "Drift Tool: transmitter name",
          QString("UNKNOWN"));

    m_propShift = GlobalProperty::registerProperty(
          "drifttool:shift",
          "Drift Tool: current shift with respect to frequency [Hz]",
          0.);

    m_propDrift = GlobalProperty::registerProperty(
          "drifttool:drift",
          "Drift Tool: current frequency drift [Hz/s]",
          0.);

    m_propVel = GlobalProperty::registerProperty(
          "drifttool:velocity",
          "Drift Tool: equivalent velocity [m/s]",
          0.);

    m_propAccel = GlobalProperty::registerProperty(
          "drifttool:acceleration",
          "Drift Tool: current acceleration [m/s^2]",
          0.);

    g_propsCreated = true;
  }

  refreshUi();
  connectAll();
}

DriftTool::~DriftTool()
{
  delete ui;
}

void
DriftTool::connectAll()
{
  connect(
        ui->openButton,
        SIGNAL(toggled(bool)),
        this,
        SLOT(onToggleOpenChannel()));

  connect(
        ui->frequencySpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onAdjust()));

  connect(
        ui->bandwidthSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onAdjust()));

  connect(
        m_spectrum,
        SIGNAL(frequencyChanged(qint64)),
        this,
        SLOT(onSpectrumFrequencyChanged(qint64)));

  connect(
        m_processor,
        SIGNAL(measurement(quint64, qreal, qreal)),
        this,
        SLOT(onMeasurement(quint64, qreal, qreal)));

  connect(
        m_processor,
        SIGNAL(stateChanged(int,QString)),
        this,
        SLOT(onChannelStateChange(int,QString)));

  connect(
        m_processor,
        SIGNAL(lockState(bool)),
        this,
        SLOT(onLockStateChanged(bool)));

  connect(
        ui->retuneCheck,
        SIGNAL(toggled(bool)),
        this,
        SLOT(onRetuneChanged()));

  connect(
        ui->retuneTriggerSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onConfigChanged()));

  connect(
        ui->logFileGroup,
        SIGNAL(toggled(bool)),
        this,
        SLOT(onToggleLog()));

  connect(
        ui->runCommandGroup,
        SIGNAL(toggled(bool)),
        this,
        SLOT(onToggleRun()));

  connect(
        ui->pllBwSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onChangeCutoff()));

  connect(
        ui->thresholdSlider,
        SIGNAL(valueChanged(int)),
        this,
        SLOT(onChangeThreshold()));

  connect(
        ui->logDirBrowseButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onBrowseLogDirectory()));

  connect(
        ui->nameEdit,
        SIGNAL(textEdited(QString)),
        this,
        SLOT(onNameChanged()));

  connect(
        ui->programBrowseButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onBrowseProgramPath()));

  connect(
        ui->refFreqSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onConfigChanged()));

  connect(
        ui->programPathEdit,
        SIGNAL(textEdited(QString)),
        this,
        SLOT(onNameChanged()));

  connect(
        ui->programArgumentsEdit,
        SIGNAL(textEdited(QString)),
        this,
        SLOT(onNameChanged()));

}

void
DriftTool::applySpectrumState()
{
  if (m_analyzer != nullptr) {
    qreal fc = SCAST(qreal, m_spectrum->getCenterFreq());
    qreal fs = SCAST(qreal, m_analyzer->getSampleRate());

    ui->frequencySpin->setMinimum(fc - .5 * fs);
    ui->frequencySpin->setMaximum(fc + .5 * fs);
  }

  onAdjust();
}

void
DriftTool::refreshNamedChannel()
{
  bool shouldHaveNamChan =
         m_analyzer != nullptr
      && m_processor->state() >= DRIFT_PROCESSOR_CONFIGURING;

  // Check whether we should have a named channel here.
  if (shouldHaveNamChan != m_haveNamChan) { // Inconsistency!
    m_haveNamChan = shouldHaveNamChan;

    // Make sure we have a named channel
    if (m_haveNamChan) {
      auto cfFreq = ui->frequencySpin->value();
      auto chBw   = m_processor->getTrueBandwidth();

      m_namChan = m_mediator->getMainSpectrum()->addChannel(
            "",
            cfFreq,
            -chBw / 2,
            +chBw / 2,
            QColor("#7f5200"),
            QColor(Qt::white),
            QColor("#7f5200"));
    } else {
      // We should NOT have a named channel, remove
      m_spectrum->removeChannel(m_namChan);
      m_spectrum->updateOverlay();
      m_propLock->setValue(QString("UNLOCKED"));
    }
  }

  if (m_haveNamChan) {
    qint64 cfFreq  = ui->frequencySpin->value();
    auto chBw      = m_processor->getTrueBandwidth();
    bool fullyOpen = m_processor->state() > DRIFT_PROCESSOR_CONFIGURING;

    QColor color;
    QColor markerColor;
    QString text;


    if (fullyOpen) {
      QString stateText;
      if (m_processor->isStable()) {
        color = QColor("#7fff7f");
        markerColor = QColor("#00ff00");
        stateText = ui->retuneCheck->isChecked() ? "TRACKING" : "STABLE";
      } else if (m_processor->hasLock()) {
        color = QColor("#ffcc7f");
        markerColor = QColor("#ffa500");
        stateText = "LOCKED";
      } else {
        color = QColor("#ff7f7f");
        markerColor = QColor("#ff0000");
        stateText = "SEARCHING";
      }

      m_propLock->setValue(stateText);

      text = QString::fromStdString(m_panelConfig->probeName) + " (" + stateText + ")";
    } else {
      color = QColor("#007f7f");
      markerColor = QColor("#007f7f");
      text  = ui->nameEdit->text() + " (opening)";
    }

    m_namChan.value()->frequency   = cfFreq;
    m_namChan.value()->lowFreqCut  = -chBw / 2;
    m_namChan.value()->highFreqCut = +chBw / 2;

    m_namChan.value()->boxColor    = color;
    m_namChan.value()->cutOffColor = markerColor;
    m_namChan.value()->markerColor = markerColor;
    m_namChan.value()->name        = text;

    m_spectrum->refreshChannel(m_namChan);
  }
}

void
DriftTool::refreshUi()
{
  bool running   = m_processor->isRunning();
  bool canRun    = m_analyzer != nullptr;
  bool canAdjust = m_processor->state() >= DRIFT_PROCESSOR_CONFIGURING;
  bool saveLogs  = !(ui->logFileGroup->isChecked()    && canAdjust);
  bool runCmd    = !(ui->runCommandGroup->isChecked() && canAdjust);

  ui->frequencySpin->setEnabled(canAdjust);
  ui->bandwidthSpin->setEnabled(canAdjust);

  BLOCKSIG_BEGIN(ui->openButton);
    ui->openButton->setEnabled(canRun);
    ui->openButton->setChecked(running);
  BLOCKSIG_END();

  BLOCKSIG(ui->thresholdSlider, setValue(m_processor->getTrueThreshold() * 100));

  ui->pllBwSpin->setEnabled(canAdjust);

  ui->retuneTriggerSpin->setEnabled(ui->retuneCheck->isChecked());
  ui->runningLed->setOn(running);
  ui->lockLed->setOn(m_processor->hasLock());
  ui->stableLed->setOn(m_processor->isStable());

  ui->runCommandLayout->setEnabled(runCmd);
  ui->logFileGroupLayout->setEnabled(saveLogs);
}

// Configuration methods
Suscan::Serializable *
DriftTool::allocConfig()
{
  return m_panelConfig = new DriftToolConfig();
}

void
DriftTool::applyConfig()
{
  setProperty("collapsed", m_panelConfig->collapsed);

  if (m_panelConfig->logDirPath == "")
    m_panelConfig->logDirPath = QDir::currentPath().toStdString();

  // Edit boxes
  BLOCKSIG(
        ui->nameEdit,
        setText(QString::fromStdString(m_panelConfig->probeName)));

  BLOCKSIG(
        ui->logDirEdit,
        setText(QString::fromStdString(m_panelConfig->logDirPath)));

  BLOCKSIG(
        ui->programPathEdit,
        setText(QString::fromStdString(m_panelConfig->programPath)));

  BLOCKSIG(
        ui->programArgumentsEdit,
        setText(QString::fromStdString(m_panelConfig->programArgs)));

  // Spinboxes
  BLOCKSIG(
        ui->refFreqSpin,
        setValue(m_panelConfig->reference));

  BLOCKSIG(
        ui->retuneTriggerSpin,
        setValue(m_panelConfig->retuneTrigger * 100.));

  BLOCKSIG(
        ui->thresholdSlider,
        setValue(m_panelConfig->lockThres * 100));

  // Checkboxes
  BLOCKSIG(
        ui->retuneCheck,
        setChecked(m_panelConfig->retune));

  BLOCKSIG(
        ui->logFileGroup,
        setChecked(m_panelConfig->logToDir));

  BLOCKSIG(
        ui->runCommandGroup,
        setChecked(m_panelConfig->runOnLock));

  // Apply to objects
  m_processor->setThreshold(m_panelConfig->lockThres);

  // Apply global properties
  m_propName->setValue(QString::fromStdString(m_panelConfig->probeName));
  m_propRef->setValue(m_panelConfig->reference);

  refreshUi();
}

bool
DriftTool::event(QEvent *event)
{
  if (event->type() == QEvent::DynamicPropertyChange) {
    QDynamicPropertyChangeEvent *const propEvent =
        static_cast<QDynamicPropertyChangeEvent*>(event);
    QString propName = propEvent->propertyName();
    if (propName == "collapsed")
      m_panelConfig->collapsed = property("collapsed").value<bool>();
  }

  return QWidget::event(event);
}

// Overriden methods
void
DriftTool::setState(int, Suscan::Analyzer *analyzer)
{
  m_analyzer = analyzer;
  m_processor->setAnalyzer(analyzer);


  if (analyzer != nullptr) {
    auto windowSize = m_mediator->getAnalyzerParams()->windowSize;
    m_processor->setFFTSizeHint(windowSize);
    applySpectrumState();
  } else {
    if (m_haveLog) {
      closeLog();
      ui->currLogFileEdit->setText("N/A");
    }
  }

  refreshNamedChannel();
  refreshUi();
}

void
DriftTool::setQth(Suscan::Location const &)
{

}

void
DriftTool::setColorConfig(ColorConfig const &)
{

}

void
DriftTool::setTimeStamp(struct timeval const &)
{

}

void
DriftTool::setProfile(Suscan::Source::Config &)
{

}

bool
DriftTool::openLog()
{
  struct timeval tv;
  struct tm parts;
  QString file;
  QString fullPath;
  QString vesselName = QString::fromStdString(m_panelConfig->probeName);
  SUSCOUNT counter = 0;

  if (m_haveLog || m_analyzer == nullptr)
    return false;

  tv = m_analyzer->getSourceTimeStamp();
  gmtime_r(&tv.tv_sec, &parts);

  if (vesselName.size() == 0) {
    vesselName = "UNKNOWN";
  } else {
    vesselName.replace(QRegExp("[^a-zA-Z\\d]"), "_");
  }

  do {
    file = vesselName + QString::asprintf(
          "_%04d%02d%02d_%02d%02d%02d_%04ld.log",
          parts.tm_year + 1900,
          parts.tm_mon + 1,
          parts.tm_mday,
          parts.tm_hour,
          parts.tm_min,
          parts.tm_sec,
          ++counter);
    fullPath = QString::fromStdString(m_panelConfig->logDirPath) + "/" + file;
  } while (QFile::exists(fullPath));

  m_logFile.setFileName(fullPath);
  if (!m_logFile.open(QIODevice::ReadWrite | QIODevice::Text)) {
    std::string error = m_logFile.errorString().toStdString();
    SU_ERROR("Cannot open %s: %s\n", fullPath.toStdString().c_str(), error.c_str());
    return false;
  }

  m_logFileName = file;
  m_logFilePath = fullPath;

  m_haveLog = true;
  return true;
}

void
DriftTool::closeLog()
{
  if (m_haveLog) {
    m_logFile.close();
    m_haveLog = false;
  }
}

void
DriftTool::logMeasurement(
    SUSCOUNT num,
    qreal full,
    qreal rel)
{
  if (m_haveLog) {
    QTextStream log(&m_logFile);
    auto  start = m_processor->getLastLock();
    qreal t0    = start.tv_sec + 1e-6 * start.tv_usec;
    qreal t     = (m_processor->getSamplesPerUpdate() * num) / m_processor->getEquivFs();
    qreal mjd   = unix2mjd(t0 + t);
    QString mjdStr = QString::asprintf("%.7lf", mjd);
    log
        << mjdStr << ","
        << num << ","
        << SCAST(int, m_processor->hasLock()) << ","
        << SCAST(int, m_processor->isStable()) << ","
        << QString::asprintf("%.12le", full) << ","
        << QString::asprintf("%.12le", rel) << "\n";
  }
}

void
DriftTool::refreshMeasurements()
{
  qreal centerFreq = SCAST(qreal, m_spectrum->getCenterFreq());
  qreal ref        = m_panelConfig->reference;
  qreal delta      = centerFreq - ref;
  qreal relShift   = m_processor->getCurrShift();
  qreal drift      = m_processor->getCurrDrift();
  qreal shift      = relShift + delta;
  qreal vel, accel;

  ui->shiftLabel->setText(SuWidgetsHelpers::formatQuantity(shift, 4, "Hz", true));
  ui->driftLabel->setText(SuWidgetsHelpers::formatQuantity(drift, 4, "Hz/s", true));

  m_propShift->setValue(shift);
  m_propDrift->setValue(drift);

  m_propFreq->setValue(centerFreq + relShift);

  if (sufreleq(ref, 0, 1)) {
    ui->velocityLabel->setText("N/A");
    ui->accelLabel->setText("N/A");
    m_propVel->setValue(0);
    m_propAccel->setValue(0);
  } else {
    vel   = shift2vel(ref, shift);
    accel = drift2accel(ref, drift);

    if (fabs(vel) >= .85 * ADSN_SPEED_OF_LIGHT) {
      ui->velocityLabel->setText("N/A");
      m_propVel->setValue(0);
    } else {
      ui->velocityLabel->setText(SuWidgetsHelpers::formatQuantity(vel, 4, "m/s", true));
      m_propVel->setValue(vel);
    }

    ui->accelLabel->setText(SuWidgetsHelpers::formatQuantity(accel, 4, "m/s²", true));
    m_propAccel->setValue(accel);
  }
}

void
DriftTool::logCurrentShift(SUSCOUNT count)
{
  qreal centerFreq = SCAST(qreal, m_spectrum->getCenterFreq());
  qreal ref        = m_panelConfig->reference;
  qreal delta      = centerFreq - ref;
  qreal relShift   = m_processor->getCurrShift();
  qreal shift      = relShift + delta;

  if (!m_haveLog) {
    if (!openLog()) {
      ui->currLogFileEdit->setStyleSheet("font-style: italic");
      ui->currLogFileEdit->setText("Failed to open log file");
      ui->logFileGroup->setChecked(false);
    } else {
      ui->currLogFileEdit->setStyleSheet("");
      ui->currLogFileEdit->setText(m_logFileName);
    }
  }

  if (m_haveLog)
    logMeasurement(
          count,
          m_processor->getCurrShift() + centerFreq,
          shift);
}

void
DriftTool::doAutoTrack(qreal chanRelShift)
{
  qreal frac       = m_panelConfig->retuneTrigger;
  qreal shift      = fabs(chanRelShift);
  qreal relShift   = m_processor->getCurrShift();
  qreal thLow, thHigh;

  thHigh =.5 * m_processor->getTrueBandwidth();
  thLow = thHigh * frac;

  if (shift >= thLow && shift < thHigh) {
    auto center = SCAST(qreal, m_spectrum->getCenterFreq());

    BLOCKSIG(ui->frequencySpin, setValue(center + relShift));
    m_processor->setFrequency(ui->frequencySpin->value());
    refreshNamedChannel();
  }
}


////////////////////////////// Slots ///////////////////////////////////////////
void
DriftTool::onToggleOpenChannel()
{
  bool open = ui->openButton->isChecked();

  if (open) {
    auto bandwidth  = m_spectrum->getBandwidth();
    auto loFreq     = m_spectrum->getLoFreq();
    auto centerFreq = m_spectrum->getCenterFreq();
    auto freq       = centerFreq + loFreq;

    BLOCKSIG(ui->bandwidthSpin, setValue(bandwidth));
    BLOCKSIG(ui->frequencySpin, setValue(freq));

    auto result = m_processor->startStreaming(freq, bandwidth);

    if (!result) {
      QMessageBox::critical(
            this,
            "Cannot open inspector",
            "Failed to open drift inspector. See log window for details");
    }
  } else {
    m_processor->cancel();
  }
}

void
DriftTool::onSpectrumFrequencyChanged(qint64)
{
  applySpectrumState();
}

void
DriftTool::onChannelStateChange(int state, QString const &desc)
{
  if (state > DRIFT_PROCESSOR_CONFIGURING) {
    BLOCKSIG_BEGIN(ui->bandwidthSpin);
      ui->bandwidthSpin->setMinimum(m_processor->getMinBandwidth());
      ui->bandwidthSpin->setMaximum(m_processor->getMaxBandwidth());
      ui->bandwidthSpin->setValue(m_processor->getTrueBandwidth());
    BLOCKSIG_END();

    BLOCKSIG_BEGIN(ui->pllBwSpin);
      ui->pllBwSpin->setMinimum(1e-3);
      ui->pllBwSpin->setMaximum(m_processor->getMaxBandwidth());
      ui->pllBwSpin->setValue(m_processor->getTrueCutOff());
    BLOCKSIG_END();
  }

  ui->stateLabel->setToolTip(desc);
  ui->driftLabel->setText("N/A");
  ui->shiftLabel->setText("N/A");
  ui->velocityLabel->setText("N/A");
  ui->accelLabel->setText("N/A");

  m_propShift->setValue(0);
  m_propDrift->setValue(0);
  m_propVel->setValue(0);
  m_propAccel->setValue(0);
  m_propFreq->setValue(0);

  setLabelTextElided(ui->stateLabel, desc);

  refreshNamedChannel();
  refreshUi();
}

void
DriftTool::onMeasurement(quint64 count, qreal chanRelShift, qreal)
{
  if (m_processor->hasLock()) {
    // Display everything on screen
    refreshMeasurements();

    // Notify
    if (!m_haveFirstReading) {
      if (m_panelConfig->runOnLock)
        notifyLock();

      m_haveFirstReading = true;
    }

    // Log this reading
    if (m_panelConfig->logToDir)
      logCurrentShift(count);

    // Do autotrack
    if (m_panelConfig->retune && m_processor->isStable())
      doAutoTrack(chanRelShift);
  }

  if (ui->stableLed->isOn() != m_processor->isStable()) {
    refreshNamedChannel();
    refreshUi();
  }
}

void
DriftTool::notifyLock()
{
  QStringList programArgs, correctedList;

  if (m_process != nullptr) {
    m_process->deleteLater();
    m_process = nullptr;
  }

  m_process = new DetachableProcess(this);

  SigDiggerHelpers::tokenize(
        QString::fromStdString(m_panelConfig->programArgs),
        programArgs);

  connect(
        m_process,
        SIGNAL(started()),
        this,
        SLOT(onProcessOpened()));

  connect(
        m_process,
        SIGNAL(errorOccurred(QProcess::ProcessError)),
        this,
        SLOT(onProcessError(QProcess::ProcessError)));

  connect(
        m_process,
        SIGNAL(finished(int,QProcess::ExitStatus)),
        this,
        SLOT(onProcessFinished(int,QProcess::ExitStatus)));

  m_process->setProcessChannelMode(QProcess::SeparateChannels);
  m_process->setInputChannelMode(QProcess::ManagedInputChannel);
  m_process->setProgram(QString::fromStdString(m_panelConfig->programPath));

  for (auto p : programArgs) {
    QString arg = p;
    arg = SigDiggerHelpers::expandGlobalProperties(arg);
    correctedList.append(arg);
  }

  m_process->setArguments(correctedList);
  m_process->start();
}

void
DriftTool::onLockStateChanged(bool)
{
  m_haveFirstReading = false;

  if (!m_processor->hasLock()) {
    ui->driftLabel->setText("N/A");
    ui->shiftLabel->setText("N/A");
    ui->velocityLabel->setText("N/A");
    ui->accelLabel->setText("N/A");

    m_propShift->setValue(0);
    m_propDrift->setValue(0);
    m_propVel->setValue(0);
    m_propAccel->setValue(0);
    m_propFreq->setValue(0);
  }

  refreshNamedChannel();
  refreshUi();
}

void
DriftTool::onAdjust()
{
  if (m_processor->state() >= DRIFT_PROCESSOR_CONFIGURING) {
    m_processor->setBandwidth(ui->bandwidthSpin->value());
    m_processor->setFrequency(ui->frequencySpin->value());
    refreshNamedChannel();
  }
}

void
DriftTool::onRetuneChanged()
{
  if (m_processor->isStable())
    refreshNamedChannel();

  onConfigChanged();
  refreshUi();
}

void
DriftTool::onToggleLog()
{
  onConfigChanged();

  if (!m_panelConfig->logToDir)
    if (m_haveLog) {
      closeLog();
      ui->currLogFileEdit->setText("N/A");
      ui->currLogFileEdit->setStyleSheet("");
    }

  refreshUi();
}

void
DriftTool::onToggleRun()
{
  onConfigChanged();
  refreshUi();
}

void
DriftTool::onChangeCutoff()
{
  m_processor->setCutOff(ui->pllBwSpin->value());
}

void
DriftTool::onChangeThreshold()
{
  onConfigChanged();
  m_processor->setThreshold(m_panelConfig->lockThres);
}

void
DriftTool::onBrowseLogDirectory()
{
  QString dir;

  dir = QFileDialog::getExistingDirectory(
        this,
        "Select log directory",
        QString::fromStdString(m_panelConfig->logDirPath));

  if (dir.size() > 0) {
    ui->logDirEdit->setText(dir);
    onConfigChanged();
  }
}

void
DriftTool::onBrowseProgramPath()
{
  QString dir;

  dir = QFileDialog::getOpenFileName(
        this,
        "Open executable",
        QString::fromStdString(m_panelConfig->programPath));


  if (dir.size() > 0) {
    ui->programPathEdit->setText(dir);
    onConfigChanged();
  }
}

void
DriftTool::onNameChanged()
{
  onConfigChanged();
  refreshNamedChannel();
}

void
DriftTool::onConfigChanged()
{
  // Edit boxes
  m_panelConfig->probeName   = ui->nameEdit->text().toStdString();
  m_panelConfig->logDirPath  = ui->logDirEdit->text().toStdString();
  m_panelConfig->programPath = ui->programPathEdit->text().toStdString();
  m_panelConfig->programArgs = ui->programArgumentsEdit->text().toStdString();

  // Spinboxes
  m_panelConfig->reference     = ui->refFreqSpin->value();
  m_panelConfig->retuneTrigger = ui->retuneTriggerSpin->value() * 1e-2;
  m_panelConfig->lockThres     = ui->thresholdSlider->value() * 1e-2;

  // Checkboxes
  m_panelConfig->retune    = ui->retuneCheck->isChecked();
  m_panelConfig->logToDir  = ui->logFileGroup->isChecked();
  m_panelConfig->runOnLock = ui->runCommandGroup->isChecked();

  m_propName->setValue(QString::fromStdString(m_panelConfig->probeName));
  m_propRef->setValue(m_panelConfig->reference);
}

void
DriftTool::onProcessOpened()
{
  m_process->detach();
}

void
DriftTool::onProcessError(QProcess::ProcessError error)
{
  const char *reason;

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

  SU_ERROR("Failed to launch program on lock: %s\n", reason);
}

void
DriftTool::onProcessFinished(int code, QProcess::ExitStatus status)
{
  QString reason;

  if (status == QProcess::CrashExit)
    SU_ERROR("Lock notifier program crashed\n");
  else if (code != 0)
    SU_ERROR("Lock notifier program finished with error status %d\n", code);
}
