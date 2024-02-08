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
#include <QClipboard>
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
  LOAD(normalize);
  LOAD(tau);
  LOAD(refbw);
  LOAD(bpe);
}

Suscan::Object &&
SNRToolConfig::serialize()
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("SNRToolConfig");

  STORE(collapsed);
  STORE(normalize);
  STORE(tau);
  STORE(refbw);
  STORE(bpe);

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

  ui->refBwSpin->setMinimum(1e-6);
  ui->refBwSpin->setMaximum(1e6);
  ui->refBwSpin->setExtraDecimals(6);
  ui->refBwSpin->setSubMultiplesAllowed(true);
  ui->refBwSpin->setAutoUnitMultiplierEnabled(true);

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

  connect(
        this->ui->snFrequencySpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onSignalNoiseAdjust()));

  connect(
        this->ui->snBandwidthSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onSignalNoiseAdjust()));

  connect(
        this->ui->nFrequencySpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onNoiseAdjust()));

  connect(
        this->ui->nBandwidthSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onNoiseAdjust()));

  connect(
        m_spectrum,
        SIGNAL(frequencyChanged(qint64)),
        this,
        SLOT(onSpectrumFrequencyChanged(qint64)));

  connect(
        ui->normalizeCheck,
        SIGNAL(toggled(bool)),
        this,
        SLOT(onConfigChanged()));

  connect(
        ui->refBwSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onConfigChanged()));

  connect(
        ui->displayBayesCheck,
        SIGNAL(toggled(bool)),
        this,
        SLOT(onConfigChanged()));

  connect(
        ui->resetBPEButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onResetBpe()));

  connect(
        ui->copyButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onCopyAll()));
}

void
SNRTool::refreshUi()
{
  bool snRunning = m_signalNoiseProcessor->isRunning();
  bool nRunning  = m_noiseProcessor->isRunning();
  bool canRun    = m_analyzer != nullptr;
  bool canAdjustSignalNoise = m_signalNoiseProcessor->state() >= POWER_PROCESSOR_CONFIGURING;
  bool canAdjustNoise = m_noiseProcessor->state() >= POWER_PROCESSOR_CONFIGURING;

  ui->snFrequencySpin->setEnabled(canAdjustSignalNoise);
  ui->snBandwidthSpin->setEnabled(canAdjustSignalNoise);

  ui->nFrequencySpin->setEnabled(canAdjustNoise);
  ui->nBandwidthSpin->setEnabled(canAdjustNoise);

  ui->resetAllButton->setEnabled(snRunning || nRunning);

  ui->snContButton->setEnabled(!snRunning && canRun);
  ui->snSingleButton->setEnabled(!snRunning && canRun);
  ui->snResetButton->setEnabled(snRunning);

  ui->nContButton->setEnabled(!nRunning && canRun);
  ui->nSingleButton->setEnabled(!nRunning && canRun);
  ui->nResetButton->setEnabled(nRunning);

  ui->sigmaNoiseLabel->setVisible(m_panelConfig->bpe);
  ui->sigmaNoiseModeLabel->setVisible(m_panelConfig->bpe);
  ui->sigmaNoiseModeDbLabel->setVisible(m_panelConfig->bpe);

  ui->sigmaSignalNoiseLabel->setVisible(m_panelConfig->bpe);
  ui->sigmaSignalNoiseModeLabel->setVisible(m_panelConfig->bpe);
  ui->sigmaSignalNoiseModeDbLabel->setVisible(m_panelConfig->bpe);

  ui->resetBPEButton->setEnabled(m_panelConfig->bpe);
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

  ui->refBwSpin->setValue(m_panelConfig->refbw);
  ui->normalizeCheck->setChecked(m_panelConfig->normalize);
  ui->displayBayesCheck->setChecked(m_panelConfig->bpe);

  m_signalNoiseProcessor->setTau(m_panelConfig->tau);
  m_noiseProcessor->setTau(m_panelConfig->tau);

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

  if (analyzer != nullptr) {
    auto windowSize = m_mediator->getAnalyzerParams()->windowSize;
    m_signalNoiseProcessor->setFFTSizeHint(windowSize);
    m_noiseProcessor->setFFTSizeHint(windowSize);
    applySpectrumState();
  }

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
  qreal esnnr, esnr;
  qreal signalNoise;
  qreal noise;
  QString units;
  const char *dbUnits;
  bool bpe = ui->displayBayesCheck->isChecked();
  qreal snScale, nScale;
  bool haveSignal, haveNoise;

  if (ui->normalizeCheck->isChecked()) {
    signalNoise = m_currentSignalNoiseDensity;
    noise       = m_currentNoiseDensity;
    snScale     = 1 / m_signalNoiseProcessor->getTrueBandwidth();
    nScale      = 1 / m_noiseProcessor->getTrueBandwidth();
    units       = "pu/Hz";
    dbUnits     = "dBpu/Hz";
  } else {
    signalNoise = m_currentSignalNoise;
    noise       = m_currentNoise;
    snScale     = 1.;
    nScale      = 1.;
    units       = "pu";
    dbUnits     = "dBpu";
  }

  // Display noise

  haveSignal = signalNoise > 0;
  haveNoise  = noise >= 0;

  if (bpe) {
    if (m_signalNoiseProcessor->haveBpe()) {
      qreal mode  = m_signalNoiseProcessor->powerModeBpe() * snScale;
      qreal delta = 5 * m_signalNoiseProcessor->powerDeltaBpe() * snScale;

      qreal modeDb       = 10 * log10(mode);
      qreal modePlusDDb  = 10 * log10(mode + delta);
      qreal deltaDb      = modePlusDDb - modeDb;

      QString strMode    = SuWidgetsHelpers::formatQuantity(mode, 7, units);
      QString strDelta   = SuWidgetsHelpers::formatQuantity(delta, 7, units);

      QString strModeDb  = QString::asprintf("%+6.3f %s", modeDb, dbUnits);
      QString strDeltaDb = QString::asprintf("%6.3f %s", deltaDb, dbUnits);

      ui->spnLabel->setText(strMode);
      ui->spnDbLabel->setText(strModeDb);
      ui->sigmaSignalNoiseModeLabel->setText(strDelta);
      ui->sigmaSignalNoiseModeDbLabel->setText(strDeltaDb);

      signalNoise = mode;
    } else {
      ui->spnLabel->setText("N/A");
      ui->spnDbLabel->setText("N/A");
      ui->sigmaSignalNoiseModeLabel->setText("N/A");
      ui->sigmaSignalNoiseModeDbLabel->setText("N/A");

      signalNoise = 0;
    }
  } else {

    if (haveSignal) {
      ui->spnLabel->setText(
            SuWidgetsHelpers::formatQuantity(signalNoise, 3, units));
      ui->spnDbLabel->setText(
            QString::asprintf("%+6.3f %s",
              SU_POWER_DB_RAW(SU_ASFLOAT(signalNoise)), dbUnits));
    } else {
      ui->spnLabel->setText("N/A");
      ui->spnDbLabel->setText("N/A");
    }
  }

  if (bpe) {
    if (m_noiseProcessor->haveBpe()) {
      qreal mode  = m_noiseProcessor->powerModeBpe() * nScale;
      qreal delta = 5 * m_noiseProcessor->powerDeltaBpe() * nScale;

      qreal modeDb       = 10 * log10(mode);
      qreal modePlusDDb  = 10 * log10(mode + delta);
      qreal deltaDb      = modePlusDDb - modeDb;

      QString strMode    = SuWidgetsHelpers::formatQuantity(mode, 7, units);
      QString strDelta   = SuWidgetsHelpers::formatQuantity(delta, 7, units);

      QString strModeDb  = QString::asprintf("%+6.3f %s", modeDb, dbUnits);
      QString strDeltaDb = QString::asprintf("%6.3f %s", deltaDb, dbUnits);

      ui->nLabel->setText(strMode);
      ui->nDbLabel->setText(strModeDb);
      ui->sigmaNoiseModeLabel->setText(strDelta);
      ui->sigmaNoiseModeDbLabel->setText(strDeltaDb);

      noise = mode;
    } else {
      ui->nLabel->setText("N/A");
      ui->nDbLabel->setText("N/A");
      ui->sigmaNoiseModeLabel->setText("N/A");
      ui->sigmaNoiseModeDbLabel->setText("N/A");

      noise = -1;
    }
  } else {
    if (haveNoise) {
      ui->nLabel->setText(
            SuWidgetsHelpers::formatQuantity(noise, 3, units));
      ui->nDbLabel->setText(
            QString::asprintf("%+6.3f %s",
              SU_POWER_DB_RAW(SU_ASFLOAT(noise)), dbUnits));
    } else {
      ui->nLabel->setText("N/A");
      ui->nDbLabel->setText("N/A");
    }
  }


  snnr = signalNoise / noise;
  if (haveSignal && haveNoise && snnr > 0) {
    ui->snnrLabel->setText(
          SuWidgetsHelpers::formatScientific(snnr));
    ui->snnrDbLabel->setText(
          QString::asprintf("%+6.3f dB",
            SU_POWER_DB_RAW(SU_ASFLOAT(snnr))));
  } else {
    ui->snnrLabel->setText("N/A");
    ui->snnrDbLabel->setText("N/A");
  }

  snr = snnr - 1;
  if (haveSignal && haveNoise && snr > 0) {
    ui->snrLabel->setText(
          SuWidgetsHelpers::formatScientific(snr));
    ui->snrDbLabel->setText(
          QString::asprintf("%+6.3f dB",
            SU_POWER_DB_RAW(SU_ASFLOAT(snr))));
  } else {
    ui->snrLabel->setText("N/A");
    ui->snrDbLabel->setText("N/A");
  }

  // The eSNR (not the eSNNR) is the easiest one to compute
  esnr = snr * m_signalNoiseWidth / m_panelConfig->refbw;
  if (haveSignal && haveNoise && esnr > 0) {
    ui->esnrLabel->setText(
          SuWidgetsHelpers::formatScientific(esnr));
    ui->esnrDbLabel->setText(
          QString::asprintf("%+6.3f dB",
            SU_POWER_DB_RAW(SU_ASFLOAT(esnr))));
  } else {
    ui->esnrLabel->setText("N/A");
    ui->esnrDbLabel->setText("N/A");
  }

  esnnr = esnr + 1;
  if (haveSignal && haveNoise && esnnr > 0) {
    ui->esnnrLabel->setText(
          SuWidgetsHelpers::formatScientific(esnnr));
    ui->esnnrDbLabel->setText(
          QString::asprintf("%+6.3f dB",
            SU_POWER_DB_RAW(SU_ASFLOAT(esnnr))));
  } else {
    ui->esnnrLabel->setText("N/A");
    ui->esnnrDbLabel->setText("N/A");
  }

  m_clipBoardText =
        "S+N:   " + ui->spnLabel->text() + " (" + ui->spnDbLabel->text()
      + ") in "
      + SuWidgetsHelpers::formatQuantity(ui->snBandwidthSpin->value(), 6, "Hz")
      + "\n"
      + "N:     " + ui->nLabel->text() + " (" + ui->nDbLabel->text()
      + ") in "
      + SuWidgetsHelpers::formatQuantity(ui->nBandwidthSpin->value(), 6, "Hz")
      + "\n"
      + "SNNR:  " + ui->snnrLabel->text() + " (" + ui->snnrDbLabel->text()
      + ")\n"
      + "SNR:   " + ui->snrLabel->text() + " (" + ui->snrDbLabel->text()
      + ")\n"
      + "eSNNR: " + ui->esnnrLabel->text() + " (" + ui->esnnrDbLabel->text()
      + ") in " + SuWidgetsHelpers::formatQuantity(ui->refBwSpin->value(), 6, "Hz")
      + "\n"
      + "eSNR:  " + ui->esnrLabel->text() + " (" + ui->esnrDbLabel->text()
      + ") in " + SuWidgetsHelpers::formatQuantity(ui->refBwSpin->value(), 6, "Hz")
      + "\n";
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

void
SNRTool::applySpectrumState()
{
  if (m_analyzer != nullptr) {
    qreal fc = SCAST(qreal, m_spectrum->getCenterFreq());
    qreal fs = SCAST(qreal, m_analyzer->getSampleRate());

    ui->snFrequencySpin->setMinimum(fc - .5 * fs);
    ui->snFrequencySpin->setMaximum(fc + .5 * fs);

    ui->nFrequencySpin->setMinimum(fc - .5 * fs);
    ui->nFrequencySpin->setMaximum(fc + .5 * fs);
  }

  onNoiseAdjust();
  onSignalNoiseAdjust();
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
SNRTool::onSignalNoiseStateChanged(int state, QString const &desc)
{
  if (state > POWER_PROCESSOR_CONFIGURING) {
    bool block;

    block = ui->snBandwidthSpin->blockSignals(true);
    ui->snBandwidthSpin->setMinimum(m_signalNoiseProcessor->getMinBandwidth());
    ui->snBandwidthSpin->setMaximum(m_signalNoiseProcessor->getMaxBandwidth());
    ui->snBandwidthSpin->setValue(m_signalNoiseProcessor->getTrueBandwidth());
    ui->snBandwidthSpin->blockSignals(block);
  }

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
    m_signalNoiseWidth = m_signalNoiseProcessor->getTrueBandwidth();
    m_noiseWidth       = m_noiseProcessor->getTrueBandwidth();
    m_widthRatio       = m_signalNoiseWidth / m_noiseWidth;
    this->refreshMeasurements();
  }
}

void
SNRTool::onNoiseStateChanged(int state, QString const &desc)
{
  if (state > POWER_PROCESSOR_CONFIGURING) {
    bool block;

    block = ui->nBandwidthSpin->blockSignals(true);
    ui->nBandwidthSpin->setMinimum(m_noiseProcessor->getMinBandwidth());
    ui->nBandwidthSpin->setMaximum(m_noiseProcessor->getMaxBandwidth());
    ui->nBandwidthSpin->setValue(m_noiseProcessor->getTrueBandwidth());
    ui->nBandwidthSpin->blockSignals(block);
  }

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
    m_signalNoiseWidth = m_signalNoiseProcessor->getTrueBandwidth();
    m_noiseWidth       = m_noiseProcessor->getTrueBandwidth();
    m_widthRatio       = m_signalNoiseWidth / m_noiseWidth;
    this->refreshMeasurements();
  }
}

void
SNRTool::onTauChanged(qreal time, qreal)
{
  m_panelConfig->tau = SU_ASFLOAT(time);

  m_signalNoiseProcessor->setTau(time);
  m_noiseProcessor->setTau(time);
}

void
SNRTool::onSignalNoiseAdjust()
{
  if (m_signalNoiseProcessor->state() >= POWER_PROCESSOR_CONFIGURING) {
    m_signalNoiseProcessor->setBandwidth(ui->snBandwidthSpin->value());
    m_signalNoiseProcessor->setFrequency(ui->snFrequencySpin->value());
    refreshSignalNoiseNamedChannel();
  }
}
void
SNRTool::onNoiseAdjust()
{
  if (m_noiseProcessor->state() >= POWER_PROCESSOR_CONFIGURING) {
    m_noiseProcessor->setBandwidth(ui->nBandwidthSpin->value());
    m_noiseProcessor->setFrequency(ui->nFrequencySpin->value());
    refreshNoiseNamedChannel();
  }
}

void
SNRTool::onSpectrumFrequencyChanged(qint64)
{
  applySpectrumState();
}

void
SNRTool::onConfigChanged()
{
  m_panelConfig->normalize = ui->normalizeCheck->isChecked();
  m_panelConfig->refbw     = ui->refBwSpin->value();

  if (m_panelConfig->bpe != ui->displayBayesCheck->isChecked()) {
    m_panelConfig->bpe       = ui->displayBayesCheck->isChecked();
    refreshUi();
  }

  refreshMeasurements();
}

void
SNRTool::onCopyAll()
{
  QApplication::clipboard()->setText(m_clipBoardText);
}

void
SNRTool::onResetBpe()
{
  m_signalNoiseProcessor->resetBpe();
  m_noiseProcessor->resetBpe();
}


