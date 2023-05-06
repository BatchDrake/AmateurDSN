//
//    DopplerTool.cpp: description
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
#include "DopplerTool.h"
#include <QEvent>
#include "ui_DopplerTool.h"
#include <UIMediator.h>
#include <MainSpectrum.h>
#include <QMessageBox>
#include "ChirpCorrector.h"

#define DOPPLERTOOL_SPEED_OF_LIGHT 299792458. // [m/s]

using namespace SigDigger;

#define STRINGFY(x) #x
#define STORE(field) obj.set(STRINGFY(field), this->field)
#define LOAD(field) this->field = conf.get(STRINGFY(field), this->field)

//////////////////////////// Widget config /////////////////////////////////////
void
DopplerToolConfig::deserialize(Suscan::Object const &conf)
{
  LOAD(collapsed);
  LOAD(velocity);
  LOAD(accel);
  LOAD(bias);
  LOAD(enabled);
}

Suscan::Object &&
DopplerToolConfig::serialize()
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("DopplerToolConfig");

  STORE(collapsed);
  STORE(velocity);
  STORE(accel);
  STORE(bias);
  STORE(enabled);

  return persist(obj);
}

/////////////////////////// Widget implementation //////////////////////////////
DopplerTool::DopplerTool(
    DopplerToolFactory *factory,
    UIMediator *mediator,
    QWidget *parent) :
  ToolWidget(factory, mediator, parent),
  ui(new Ui::DopplerTool)
{
  ui->setupUi(this);

  m_corrector = new ChirpCorrector();
  assertConfig();

  m_spectrum = mediator->getMainSpectrum();
  setProperty("collapsed", m_panelConfig->collapsed);

  refreshUi();
  connectAll();
}

DopplerTool::~DopplerTool()
{
  delete m_corrector;
  delete ui;
}

void
DopplerTool::setFromVelocity(qreal velocity)
{
  SUFREQ freq = m_mediator->getCurrentCenterFreq();

  // From the Doppler-Fizeau shift equation:
  //
  //         c
  // Δf = - --- Δv
  //         f
  //

  m_panelConfig->velocity = velocity;
  m_currResetFreq         = -DOPPLERTOOL_SPEED_OF_LIGHT / freq * velocity;

  m_corrector->setResetFrequency(m_currResetFreq);
}

void
DopplerTool::setFromShift(qreal shift)
{
  SUFREQ freq = m_mediator->getCurrentCenterFreq();

  m_panelConfig->velocity = -freq / DOPPLERTOOL_SPEED_OF_LIGHT * shift;
  m_currResetFreq         = shift;

  m_corrector->setResetFrequency(m_currResetFreq);
}

void
DopplerTool::setFromAccel(qreal accel)
{
  SUFREQ freq = m_mediator->getCurrentCenterFreq();

  // From the Doppler-Fizeau shift equation:
  //
  //         c         dΔf       c   dΔv       c
  // Δf = - --- Δv => ----- = - --- ----- = - --- a
  //         f         dt        f    dt       f
  //

  m_panelConfig->accel = accel;
  m_currRate           = -DOPPLERTOOL_SPEED_OF_LIGHT / freq * accel;
  m_correctedRate      = m_currRate + m_panelConfig->bias;

  m_corrector->setChirpRate(m_correctedRate);
}

void
DopplerTool::setFromRate(qreal rate)
{
  SUFREQ freq = m_mediator->getCurrentCenterFreq();

  // See setFromAccel for details

  m_panelConfig->accel = -freq / DOPPLERTOOL_SPEED_OF_LIGHT * rate;
  m_currRate           = rate;
  m_correctedRate      = rate + m_panelConfig->bias;

  m_corrector->setChirpRate(m_correctedRate);
}

void
DopplerTool::connectAll()
{
  connect(
        m_mediator,
        SIGNAL(frequencyChanged(qint64,qint64)),
        this,
        SLOT(onFrequencyChanged()));

  connect(
        ui->velSpinBox,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onVelChanged()));

  connect(
        ui->accelSpinBox,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onAccelChanged()));

  connect(
        ui->freqSpinBox,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onShiftChanged()));

  connect(
        ui->freqRateSpinBox,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onRateChanged()));

  connect(
        ui->rateBiasSpinBox,
        SIGNAL(valueChanged(double)),
        this,
        SLOT(onBiasChanged()));

  connect(
        ui->resetButton,
        SIGNAL(clicked(bool)),
        this,
        SLOT(onReset()));

  connect(
        ui->enableButton,
        SIGNAL(toggled(bool)),
        this,
        SLOT(onToggleEnabled()));
}

void
DopplerTool::refreshUi()
{
  bool prev = enterChangeState();

  ui->velSpinBox->setValue(m_panelConfig->velocity);
  ui->accelSpinBox->setValue(m_panelConfig->accel);

  ui->freqSpinBox->setValue(m_currResetFreq);
  ui->freqRateSpinBox->setValue(m_currRate);
  ui->rateBiasSpinBox->setValue(m_panelConfig->bias);

  ui->enableButton->setChecked(m_panelConfig->enabled);

  leaveChangeState(prev);
}

void
DopplerTool::applySpectrumState()
{
  setFromVelocity(m_panelConfig->velocity);
  setFromAccel(m_panelConfig->accel);
  refreshUi();
}

bool
DopplerTool::enterChangeState()
{
  bool blocked;

  blocked =
         ui->freqSpinBox->blockSignals(true)
      && ui->freqRateSpinBox->blockSignals(true)
      && ui->rateBiasSpinBox->blockSignals(true)
      && ui->velSpinBox->blockSignals(true)
      && ui->accelSpinBox->blockSignals(true)
      && ui->enableButton->blockSignals(true);

  return blocked;
}

void
DopplerTool::leaveChangeState(bool state)
{
  ui->freqSpinBox->blockSignals(state);
  ui->accelSpinBox->blockSignals(state);
  ui->velSpinBox->blockSignals(state);
  ui->freqRateSpinBox->blockSignals(state);
  ui->rateBiasSpinBox->blockSignals(state);
  ui->enableButton->blockSignals(state);
}

// Configuration methods
Suscan::Serializable *
DopplerTool::allocConfig()
{
  return m_panelConfig = new DopplerToolConfig();
}

void
DopplerTool::applyConfig()
{
  setProperty("collapsed", m_panelConfig->collapsed);

  setFromVelocity(m_panelConfig->velocity);
  setFromAccel(m_panelConfig->accel);

  m_corrector->setEnabled(m_panelConfig->enabled);

  refreshUi();
}

bool
DopplerTool::event(QEvent *event)
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
DopplerTool::setState(int, Suscan::Analyzer *analyzer)
{
  m_analyzer = analyzer;

  if (analyzer != nullptr)
    applySpectrumState();

  m_corrector->setAnalyzer(m_analyzer);

  refreshUi();
}

void
DopplerTool::setQth(Suscan::Location const &)
{

}

void
DopplerTool::setColorConfig(ColorConfig const &)
{

}

void
DopplerTool::setTimeStamp(struct timeval const &)
{

}

void
DopplerTool::setProfile(Suscan::Source::Config &)
{

}


////////////////////////////// Slots //////////////////////////////////////////
void
DopplerTool::onVelChanged()
{
  setFromVelocity(ui->velSpinBox->value());
  refreshUi();
}

void
DopplerTool::onAccelChanged()
{
  setFromAccel(ui->accelSpinBox->value());
  refreshUi();
}

void
DopplerTool::onShiftChanged()
{
  setFromShift(ui->freqSpinBox->value());
  refreshUi();
}

void
DopplerTool::onRateChanged()
{
  setFromRate(ui->freqRateSpinBox->value());
  refreshUi();
}

void
DopplerTool::onBiasChanged()
{
  m_panelConfig->bias = ui->rateBiasSpinBox->value();
  setFromAccel(m_panelConfig->accel);
  refreshUi();
}

void
DopplerTool::onReset()
{
  m_corrector->reset();
}

void
DopplerTool::onToggleEnabled()
{
  m_panelConfig->enabled = ui->enableButton->isChecked();
  m_corrector->setEnabled(m_panelConfig->enabled);
}

void
DopplerTool::onFrequencyChanged()
{
  applySpectrumState();
}
