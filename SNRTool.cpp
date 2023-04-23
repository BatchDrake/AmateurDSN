//
//    SNRTool.cpp: description
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
#include "SNRTool.h"
#include <QEvent>
#include "ui_SNRTool.h"
#include <UIMediator.h>
#include <MainSpectrum.h>
#include <PowerProcessor.h>
#include <QMessageBox>
#include <Suscan/AnalyzerRequestTracker.h>

using namespace SigDigger;

#define STRINGFY(x) #x
#define STORE(field) obj.set(STRINGFY(field), this->field)
#define LOAD(field) this->field = conf.get(STRINGFY(field), this->field)

//////////////////////////// Widget config /////////////////////////////////////
void
SNRToolConfig::deserialize(Suscan::Object const &conf)
{
  LOAD(collapsed);
  LOAD(tau);
}

Suscan::Object &&
SNRToolConfig::serialize()
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("SNRToolConfig");

  STORE(collapsed);
  STORE(tau);

  return persist(obj);
}

/////////////////////////// Widget implementation //////////////////////////////
SNRTool::SNRTool(
    SNRToolFactory *factory,
    UIMediator *mediator,
    QWidget *parent) :
  ToolWidget(factory, mediator, parent),
  ui(new Ui::SNRTool)
{
  ui->setupUi(this);

  assertConfig();

  m_signalNoiseProcessor = new PowerProcessor(mediator, this);
  m_noiseProcessor       = new PowerProcessor(mediator, this);
  m_spectrum             = mediator->getMainSpectrum();

  setProperty("collapsed", m_panelConfig->collapsed);

  refreshUi();
  refreshMeasurements();
  connectAll();
}

SNRTool::~SNRTool()
{
  delete ui;
}

void
SNRTool::connectAll()
{
  connect(
        this->m_signalNoiseProcessor,
        SIGNAL(measurement(qreal)),
        this,
        SLOT(onSignalNoiseMeasurement(qreal)));

  connect(
        this->m_signalNoiseProcessor,
        SIGNAL(stateChanged(int,QString)),
        this,
        SLOT(onSignalNoiseStateChanged(int,QString)));

  connect(
        this->m_noiseProcessor,
        SIGNAL(measurement(qreal)),
        this,
        SLOT(onNoiseMeasurement(qreal)));

  connect(
        this->m_noiseProcessor,
        SIGNAL(stateChanged(int,QString)),
        this,
        SLOT(onNoiseStateChanged(int,QString)));

  connect(
        this->ui->tauSpinBox,
        SIGNAL(changed(qreal,qreal)),
        this,
        SLOT(onTauChanged(qreal,qreal)));

  connect(
        this->ui->snContButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onSignalNoiseCont()));

  connect(
        this->ui->snSingleButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onSignalNoiseSingle()));

  connect(
        this->ui->snResetButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onSignalNoiseCancel()));

  connect(
        this->ui->nContButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onNoiseCont()));

  connect(
        this->ui->nSingleButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onNoiseSingle()));

  connect(
        this->ui->nResetButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onNoiseCancel()));

  connect(
        this->ui->resetAllButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onNoiseCancel()));

  connect(
        this->ui->resetAllButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onSignalNoiseCancel()));
}

void
SNRTool::refreshUi()
{
  bool snRunning = m_signalNoiseProcessor->isRunning();
  bool nRunning  = m_noiseProcessor->isRunning();
  bool canRun    = m_analyzer != nullptr;

  ui->resetAllButton->setEnabled(snRunning || nRunning);

  ui->snContButton->setEnabled(!snRunning && canRun);
  ui->snSingleButton->setEnabled(!snRunning && canRun);
  ui->snResetButton->setEnabled(snRunning);

  ui->nContButton->setEnabled(!nRunning && canRun);
  ui->nSingleButton->setEnabled(!nRunning && canRun);
  ui->nResetButton->setEnabled(nRunning);
}

// Configuration methods
Suscan::Serializable *
SNRTool::allocConfig()
{
  return m_panelConfig = new SNRToolConfig();
}

void
SNRTool::applyConfig()
{
  setProperty("collapsed", m_panelConfig->collapsed);
  ui->tauSpinBox->setTimeMin(1e-3);
  ui->tauSpinBox->setTimeMax(86400);

  ui->tauSpinBox->setTimeValue(SCAST(qreal, m_panelConfig->tau));
  ui->tauSpinBox->setBestUnits(true);

  refreshUi();
}
bool
SNRTool::event(QEvent *event)
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

// Overridenn methods
void
SNRTool::setState(int, Suscan::Analyzer *analyzer)
{
  m_analyzer = analyzer;

  m_signalNoiseProcessor->setAnalyzer(analyzer);
  m_noiseProcessor->setAnalyzer(analyzer);

  refreshUi();
}

void
SNRTool::setQth(Suscan::Location const &)
{

}

void
SNRTool::setColorConfig(ColorConfig const &)
{

}

void
SNRTool::setTimeStamp(struct timeval const &)
{

}

void
SNRTool::setProfile(Suscan::Source::Config &)
{

}

bool
SNRTool::isFrozen() const
{
  return ui->freezeButton->isChecked();
}

void
SNRTool::refreshSignalNoiseNamedChannel()
{
  bool shouldHaveNamChan =
         m_analyzer != nullptr
      && m_signalNoiseProcessor->state() >= POWER_PROCESSOR_CONFIGURING;

  // Check whether we should have a named channel here.
  if (shouldHaveNamChan != m_haveSignalNoiseNamChan) { // Inconsistency!
    m_haveSignalNoiseNamChan = shouldHaveNamChan;

    // Make sure we have a named channel
    if (m_haveSignalNoiseNamChan) {
      auto cfFreq = ui->snFrequencySpin->value();
      auto chBw   = m_signalNoiseProcessor->getTrueBandwidth();

      m_signalNoiseNamChan = this->m_mediator->getMainSpectrum()->addChannel(
            "",
            cfFreq,
            -chBw / 2,
            +chBw / 2,
            QColor("#7f5200"),
            QColor(Qt::white),
            QColor("#7f5200"));
    } else {
      // We should NOT have a named channel, remove
      m_spectrum->removeChannel(m_signalNoiseNamChan);
      m_spectrum->updateOverlay();
    }
  }

  if (m_haveSignalNoiseNamChan) {
    qint64 cfFreq  = ui->snFrequencySpin->value();
    auto chBw      = m_signalNoiseProcessor->getTrueBandwidth();
    bool fullyOpen = m_signalNoiseProcessor->state() > POWER_PROCESSOR_CONFIGURING;

    QColor color       = fullyOpen ? QColor("#ffa500") : QColor("#7f5200");
    QColor markerColor = fullyOpen ? QColor("#ffa500") : QColor("#7f5200");
    QString text;


    if (fullyOpen) {
      text = "Signal probe ("
          + SuWidgetsHelpers::formatQuantity(m_signalNoiseProcessor->getMaxBandwidth(), 3, "Hz")
          + ")";
    } else {
      text = "Signal probe (opening)";
    }

    m_signalNoiseNamChan.value()->frequency   = cfFreq;
    m_signalNoiseNamChan.value()->lowFreqCut  = -chBw / 2;
    m_signalNoiseNamChan.value()->highFreqCut = +chBw / 2;

    m_signalNoiseNamChan.value()->boxColor    = color;
    m_signalNoiseNamChan.value()->cutOffColor = color;
    m_signalNoiseNamChan.value()->markerColor = markerColor;
    m_signalNoiseNamChan.value()->name        = text;

    m_spectrum->refreshChannel(m_signalNoiseNamChan);
  }
}

void
SNRTool::refreshNoiseNamedChannel()
{
  bool shouldHaveNamChan =
         m_analyzer != nullptr
      && m_noiseProcessor->state() >= POWER_PROCESSOR_CONFIGURING;

  // Check whether we should have a named channel here.
  if (shouldHaveNamChan != m_haveNoiseNamChan) { // Inconsistency!
    m_haveNoiseNamChan = shouldHaveNamChan;

    // Make sure we have a named channel
    if (m_haveNoiseNamChan) {
      auto cfFreq = ui->nFrequencySpin->value();
      auto chBw   = m_noiseProcessor->getTrueBandwidth();

      m_noiseNamChan = this->m_mediator->getMainSpectrum()->addChannel(
            "",
            cfFreq,
            -chBw / 2,
            +chBw / 2,
            QColor("#7f5200"),
            QColor(Qt::white),
            QColor("#7f5200"));
    } else {
      // We should NOT have a named channel, remove
      m_spectrum->removeChannel(m_noiseNamChan);
      m_spectrum->updateOverlay();
    }
  }

  if (m_haveNoiseNamChan) {
    qint64 cfFreq  = ui->nFrequencySpin->value();
    auto chBw      = m_noiseProcessor->getTrueBandwidth();
    bool fullyOpen = m_noiseProcessor->state() > POWER_PROCESSOR_CONFIGURING;

    QColor color       = fullyOpen ? QColor("#00ffff") : QColor("#007f7f");
    QColor markerColor = fullyOpen ? QColor("#00ffff") : QColor("#007f7f");
    QString text;


    if (fullyOpen) {
      text = "Noise probe ("
          + SuWidgetsHelpers::formatQuantity(m_noiseProcessor->getMaxBandwidth(), 3, "Hz")
          + ")";
    } else {
      text = "Noise probe (opening)";
    }

    m_noiseNamChan.value()->frequency   = cfFreq;
    m_noiseNamChan.value()->lowFreqCut  = -chBw / 2;
    m_noiseNamChan.value()->highFreqCut = +chBw / 2;

    m_noiseNamChan.value()->boxColor    = color;
    m_noiseNamChan.value()->cutOffColor = color;
    m_noiseNamChan.value()->markerColor = markerColor;
    m_noiseNamChan.value()->name        = text;

    m_spectrum->refreshChannel(m_noiseNamChan);
  }
}

void
SNRTool::refreshNamedChannels()
{
  refreshSignalNoiseNamedChannel();
  refreshNoiseNamedChannel();
}

void
SNRTool::refreshMeasurements()
{
  qreal snnr, snr;
  qreal signalNoise;
  qreal noise;
  QString units;
  const char *dbUnits;

  if (ui->normalizeCheck->isChecked()) {
    signalNoise = m_currentSignalNoiseDensity;
    noise       = m_currentNoiseDensity;
    units       = "pu/Hz";
    dbUnits     = "dBpu/Hz";
  } else {
    signalNoise = m_currentSignalNoise;
    noise       = m_currentNoise;
    units       = "pu";
    dbUnits     = "dBpu";
  }

  if (signalNoise <= 0) {
    ui->spnLabel->setText("N/A");
    ui->spnDbLabel->setText("N/A");
  } else {
    ui->spnLabel->setText(
          SuWidgetsHelpers::formatQuantity(signalNoise, 3, units));
    ui->spnDbLabel->setText(
          QString::asprintf("%+6.3f %s",
            SU_POWER_DB_RAW(SU_ASFLOAT(signalNoise)), dbUnits));
  }

  if (noise <= 0) {
    ui->nLabel->setText("N/A");
    ui->nDbLabel->setText("N/A");
  } else {
    ui->nLabel->setText(
          SuWidgetsHelpers::formatQuantity(noise, 3, units));
    ui->nDbLabel->setText(
          QString::asprintf("%+6.3f %s",
            SU_POWER_DB_RAW(SU_ASFLOAT(noise)), dbUnits));
  }

  snnr = signalNoise / noise;
  if (snnr <= 0) {
    ui->snrLabel->setText("N/A");
    ui->snnrDbLabel->setText("N/A");
  } else {
    ui->snnrLabel->setText(
          SuWidgetsHelpers::formatScientific(snnr));
    ui->snnrDbLabel->setText(
          QString::asprintf("%+6.3f %s",
            SU_POWER_DB_RAW(SU_ASFLOAT(snnr)), dbUnits));
  }

  snr = (signalNoise - noise) / noise;
  if (snr <= 0) {
    ui->snrLabel->setText("N/A");
    ui->snrDbLabel->setText("N/A");
  } else {
    ui->snrLabel->setText(
          SuWidgetsHelpers::formatScientific(snr));
    ui->snrDbLabel->setText(
          QString::asprintf("%+6.3f %s",
            SU_POWER_DB_RAW(SU_ASFLOAT(snr)), dbUnits));
  }
}

void
SNRTool::openSignalNoiseProbe(bool hold)
{
  auto bandwidth  = m_spectrum->getBandwidth();
  auto loFreq     = m_spectrum->getLoFreq();
  auto centerFreq = m_spectrum->getCenterFreq();
  auto freq       = centerFreq + loFreq;
  bool result;

  bool bwBlocked = ui->snBandwidthSpin->blockSignals(true);
  bool fcBlocked = ui->snFrequencySpin->blockSignals(true);

  ui->snBandwidthSpin->setValue(bandwidth);
  ui->snFrequencySpin->setValue(freq);

  ui->snBandwidthSpin->blockSignals(bwBlocked);
  ui->snFrequencySpin->blockSignals(fcBlocked);

  if (m_analyzer != nullptr) {
    if (hold)
      result = m_signalNoiseProcessor->startStreaming(freq, bandwidth);
    else
      result = m_signalNoiseProcessor->oneShot(freq, bandwidth);

    if (!result) {
      QMessageBox::critical(
            this,
            "Cannot open inspector",
            "Failed to open power inspector. See log window for details");
    }
  }
}

void
SNRTool::openNoiseProbe(bool hold)
{
  auto bandwidth  = m_spectrum->getBandwidth();
  auto loFreq     = m_spectrum->getLoFreq();
  auto centerFreq = m_spectrum->getCenterFreq();
  auto freq       = centerFreq + loFreq;
  bool result;

  bool bwBlocked = ui->nBandwidthSpin->blockSignals(true);
  bool fcBlocked = ui->nFrequencySpin->blockSignals(true);

  ui->nBandwidthSpin->setValue(bandwidth);
  ui->nFrequencySpin->setValue(freq);

  ui->nBandwidthSpin->blockSignals(bwBlocked);
  ui->nFrequencySpin->blockSignals(fcBlocked);


  if (m_analyzer != nullptr) {
    if (hold)
      result = m_noiseProcessor->startStreaming(freq, bandwidth);
    else
      result = m_noiseProcessor->oneShot(freq, bandwidth);

    if (!result) {
      QMessageBox::critical(
            this,
            "Cannot open inspector",
            "Failed to open power inspector. See log window for details");
    }
  }
}

void
SNRTool::cancelSignalNoiseProbe()
{
  m_signalNoiseProcessor->cancel();
}

void
SNRTool::cancelNoiseProbe()
{
  m_noiseProcessor->cancel();
}


////////////////////////////// Slots //////////////////////////////////////////
void
SNRTool::onSignalNoiseCont()
{
  openSignalNoiseProbe(true);
}

void
SNRTool::onSignalNoiseSingle()
{
  openSignalNoiseProbe(false);
}

void
SNRTool::onSignalNoiseCancel()
{
  cancelSignalNoiseProbe();
}

void
SNRTool::onNoiseCont()
{
  openNoiseProbe(true);
}

void
SNRTool::onNoiseSingle()
{
  openNoiseProbe(false);
}

void
SNRTool::onNoiseCancel()
{
  cancelNoiseProbe();
}

void
SNRTool::onSignalNoiseStateChanged(int, QString const &desc)
{
  ui->snStateLabel->setText(desc);
  refreshSignalNoiseNamedChannel();
  refreshUi();
}

void
SNRTool::onSignalNoiseMeasurement(qreal reading)
{
  if (!this->isFrozen()) {
    m_currentSignalNoise = reading;
    m_currentSignalNoiseDensity = reading / m_signalNoiseProcessor->getTrueBandwidth();
    this->refreshMeasurements();
  }
}

void
SNRTool::onNoiseStateChanged(int, QString const &desc)
{
  ui->nStateLabel->setText(desc);
  refreshNoiseNamedChannel();
  refreshUi();
}

void
SNRTool::onNoiseMeasurement(qreal reading)
{
  if (!this->isFrozen()) {
    m_currentNoise = reading;
    m_currentNoiseDensity = reading / m_noiseProcessor->getTrueBandwidth();
    this->refreshMeasurements();
  }
}

void
SNRTool::onTauChanged(qreal time, qreal)
{
  m_signalNoiseProcessor->setTau(time);
  m_noiseProcessor->setTau(time);
}
