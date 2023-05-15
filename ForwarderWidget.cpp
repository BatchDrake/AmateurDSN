//
//    ForwarderWidget.cpp: description
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
#include "ForwarderWidget.h"
#include "ProcessForwarder.h"
#include "ui_ForwarderWidget.h"
#include <QFileDialog>
#include <QMouseEvent>
#include <QInputDialog>
#include <UIMediator.h>
#include <MainSpectrum.h>
#include <SigDiggerHelpers.h>

using namespace SigDigger;

#define STRINGFY(x) #x
#define STORE(field) obj.set(STRINGFY(field), this->field)
#define LOAD(field) this->field = conf.get(STRINGFY(field), this->field)


//////////////////////////// Widget config /////////////////////////////////////
void
ForwarderWidgetConfig::deserialize(Suscan::Object const &conf)
{
  LOAD(programPath);
  LOAD(arguments);
  LOAD(title);
}

Suscan::Object &&
ForwarderWidgetConfig::serialize()
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("ForwarderWidgetConfig");

  STORE(programPath);
  STORE(arguments);
  STORE(title);

  return persist(obj);
}

/////////////////////////// Widget implementation //////////////////////////////
ForwarderWidget::ForwarderWidget(UIMediator *mediator, QWidget *parent) :
  QWidget(parent),
  ui(new Ui::ForwarderWidget)
{
  ui->setupUi(this);

  m_spectrum  = mediator->getMainSpectrum();
  m_forwarder = new ProcessForwarder(mediator, this);
  m_mediator  = mediator;

  connectAll();
  refreshUi();
}

ForwarderWidget::~ForwarderWidget()
{
  delete ui;
}

void
ForwarderWidget::refreshUi()
{
  bool haveAnalyzer = m_analyzer != nullptr;

  ui->openButton->setEnabled(haveAnalyzer && m_forwarder->state() == PROCESS_FORWARDER_IDLE);
  ui->browseButton->setEnabled(m_forwarder->state() == PROCESS_FORWARDER_IDLE);
  ui->detachButton->setEnabled(haveAnalyzer && m_forwarder->state() == PROCESS_FORWARDER_RUNNING);
  ui->terminateButton->setEnabled(haveAnalyzer && m_forwarder->state() != PROCESS_FORWARDER_IDLE);

  ui->bandwidthSpin->setEnabled(haveAnalyzer && m_forwarder->state() == PROCESS_FORWARDER_RUNNING);
  ui->frequencySpin->setEnabled(haveAnalyzer && m_forwarder->state() == PROCESS_FORWARDER_RUNNING);
}

void
ForwarderWidget::connectAll()
{
  connect(
        m_forwarder,
        SIGNAL(stateChanged(int,QString)),
        this,
        SLOT(onForwarderStateChanged(int,QString)));

  connect(
        ui->openButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onOpen()));

  connect(
        ui->terminateButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onTerminate()));

  connect(
        ui->detachButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onDetach()));

  connect(
        ui->frequencySpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onAdjustFrequency()));

  connect(
        ui->bandwidthSpin,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onAdjustBandwidth()));

  connect(
        ui->browseButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onBrowse()));

  connect(
        ui->programPathEdit,
        SIGNAL(textEdited(QString)),
        this,
        SLOT(onConfigChanged()));

  connect(
        ui->argumentEdit,
        SIGNAL(textEdited(QString)),
        this,
        SLOT(onConfigChanged()));
}

void
ForwarderWidget::refreshNamedChannel()
{
  bool shouldHaveNamChan =
         m_analyzer != nullptr
      && m_forwarder->state() >= PROCESS_FORWARDER_OPENING;

  // Check whether we should have a named channel here.
  if (shouldHaveNamChan != m_haveNamChan) { // Inconsistency!
    m_haveNamChan = shouldHaveNamChan;

    // Make sure we have a named channel
    if (m_haveNamChan) {
      auto cfFreq = ui->frequencySpin->value();
      auto chBw   = m_forwarder->getTrueBandwidth();

      m_namChan = this->m_mediator->getMainSpectrum()->addChannel(
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
    }
  }

  if (m_haveNamChan) {
    qint64 cfFreq  = ui->frequencySpin->value();
    auto chBw      = m_forwarder->getTrueBandwidth();
    bool fullyOpen = m_forwarder->state() == PROCESS_FORWARDER_RUNNING;

    QColor color       = fullyOpen ? QColor("#007f00") : QColor("#003f00");
    QColor markerColor = fullyOpen ? QColor("#007f00") : QColor("#003f00");
    QString text;
    QString name = ui->groupBox->title();

    if (fullyOpen) {
      text = name + " ("
          + SuWidgetsHelpers::formatQuantity(m_forwarder->getMaxBandwidth(), 3, "Hz")
          + ")";
    } else {
      text = name + " (opening)";
    }

    m_namChan.value()->frequency   = cfFreq;
    m_namChan.value()->lowFreqCut  = -chBw / 2;
    m_namChan.value()->highFreqCut = +chBw / 2;

    m_namChan.value()->boxColor    = color;
    m_namChan.value()->cutOffColor = color;
    m_namChan.value()->markerColor = markerColor;
    m_namChan.value()->name        = text;

    m_spectrum->refreshChannel(m_namChan);
  }
}

void
ForwarderWidget::applySpectrumState()
{
  if (m_analyzer != nullptr) {
    qreal fc = SCAST(qreal, m_spectrum->getCenterFreq());
    qreal fs = SCAST(qreal, m_analyzer->getSampleRate());

    ui->frequencySpin->setMinimum(fc - .5 * fs);
    ui->frequencySpin->setMaximum(fc + .5 * fs);
  }
}

void
ForwarderWidget::setState(int, Suscan::Analyzer *analyzer)
{
  m_analyzer = analyzer;

  m_forwarder->setAnalyzer(analyzer);

  if (analyzer != nullptr) {
    auto windowSize = m_mediator->getAnalyzerParams()->windowSize;
    m_forwarder->setFFTSizeHint(windowSize);
    applySpectrumState();
  }

  refreshUi();
}

void
ForwarderWidget::setProgramPath(QString const &path)
{
  ui->programPathEdit->setText(path);
  ui->programPathEdit->setCursorPosition(0);
}

void
ForwarderWidget::setArguments(QString const &args)
{
  ui->argumentEdit->setText(args);
  ui->argumentEdit->setCursorPosition(0);
}

void
ForwarderWidget::setFrequency(qreal freq)
{
  bool block;

  m_forwarder->setFrequency(freq);

  block = ui->bandwidthSpin->blockSignals(true);
  ui->bandwidthSpin->setValue(m_forwarder->getTrueBandwidth());
  ui->bandwidthSpin->blockSignals(block);
}

void
ForwarderWidget::setBandwidth(qreal bw)
{
  bool block;

  m_forwarder->setBandwidth(bw);

  block = ui->bandwidthSpin->blockSignals(true);
  ui->bandwidthSpin->setValue(m_forwarder->getTrueBandwidth());
  ui->bandwidthSpin->blockSignals(block);
}

void
ForwarderWidget::setName(QString const &name)
{
  ui->groupBox->setTitle(name);
  m_config.title = name.toStdString();
  refreshNamedChannel();
}

QString
ForwarderWidget::programPath() const
{
  return ui->programPathEdit->text();
}

QString
ForwarderWidget::arguments() const
{
  return ui->argumentEdit->text();
}

void
ForwarderWidget::mouseDoubleClickEvent(QMouseEvent *ev)
{
  if (ev->button() == Qt::LeftButton) {
    bool ok;
    QString text = QInputDialog::getText(
          this,
          "Change preset name",
          "Name of this preset:",
          QLineEdit::Normal,
          ui->groupBox->title(),
          &ok);
    if (ok && text.size() > 0) {
      setName(text);
      emit configChanged();
    }
  }
}

void
ForwarderWidget::setConfig(ForwarderWidgetConfig const &config)
{
  m_config = config;

  setName(QString::fromStdString(m_config.title));
  setProgramPath(QString::fromStdString(m_config.programPath));
  setArguments(QString::fromStdString(m_config.arguments));
}

ForwarderWidgetConfig const &
ForwarderWidget::getConfig() const
{
  return m_config;
}

/////////////////////////////////// Slots //////////////////////////////////////
void
ForwarderWidget::onOpen()
{
  QStringList argList;
  auto bandwidth  = m_spectrum->getBandwidth();
  auto loFreq     = m_spectrum->getLoFreq();
  auto centerFreq = m_spectrum->getCenterFreq();
  auto freq       = centerFreq + loFreq;

  bool bwBlocked = ui->bandwidthSpin->blockSignals(true);
  bool fcBlocked = ui->frequencySpin->blockSignals(true);

  SigDiggerHelpers::tokenize(arguments(), argList);

  ui->bandwidthSpin->setValue(bandwidth);
  ui->frequencySpin->setValue(freq);

  ui->bandwidthSpin->blockSignals(bwBlocked);
  ui->frequencySpin->blockSignals(fcBlocked);

  if (!m_forwarder->run(
        programPath(),
        argList,
        ui->frequencySpin->value(),
        ui->bandwidthSpin->value())) {
    QMessageBox::warning(
          this,
          "Command failed",
          "Cannot open a channel in the current state");
  }
}

void
ForwarderWidget::onTerminate()
{
  m_forwarder->cancel();
}

void
ForwarderWidget::onDetach()
{
  m_forwarder->detach();
}

void
ForwarderWidget::onAdjustBandwidth()
{
  if (m_forwarder->state() >= PROCESS_FORWARDER_OPENING) {
    m_forwarder->setBandwidth(ui->bandwidthSpin->value());
    this->setBandwidth(m_forwarder->getTrueBandwidth());
    refreshNamedChannel();
  }
}

void
ForwarderWidget::onAdjustFrequency()
{
  if (m_forwarder->state() >= PROCESS_FORWARDER_OPENING) {
    m_forwarder->setFrequency(ui->frequencySpin->value());
    refreshNamedChannel();
  }
}


void
ForwarderWidget::onForwarderStateChanged(int state, QString const &desc)
{
  if (state > PROCESS_FORWARDER_OPENING) {
    bool block;

    block = ui->bandwidthSpin->blockSignals(true);
    ui->bandwidthSpin->setMinimum(m_forwarder->getMinBandwidth());
    ui->bandwidthSpin->setMaximum(m_forwarder->getMaxBandwidth());
    ui->bandwidthSpin->setValue(m_forwarder->getTrueBandwidth());
    ui->bandwidthSpin->blockSignals(block);
  }

  ui->stateLabel->setText(desc);
  refreshNamedChannel();
  refreshUi();
}

void
ForwarderWidget::onBrowse()
{
  QDir d = QFileInfo(this->programPath()).absoluteDir();
  QString path = QFileDialog::getOpenFileName(
        this,
        "Open executable",
        d.absolutePath());

  if (path.size() > 0)
    setProgramPath(path);
}

void
ForwarderWidget::onConfigChanged()
{
  m_config.programPath = ui->programPathEdit->text().toStdString();
  m_config.arguments   = ui->argumentEdit->text().toStdString();

  emit configChanged();
}
