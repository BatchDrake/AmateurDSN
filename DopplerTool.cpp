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

using namespace SigDigger;

#define STRINGFY(x) #x
#define STORE(field) obj.set(STRINGFY(field), this->field)
#define LOAD(field) this->field = conf.get(STRINGFY(field), this->field)

//////////////////////////// Widget config /////////////////////////////////////
void
DopplerToolConfig::deserialize(Suscan::Object const &conf)
{
  LOAD(collapsed);
  LOAD(speed);
  LOAD(bias);
  LOAD(enabled);
}

Suscan::Object &&
DopplerToolConfig::serialize()
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("DopplerToolConfig");

  STORE(collapsed);
  STORE(speed);
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

  assertConfig();

  m_spectrum = mediator->getMainSpectrum();
  setProperty("collapsed", m_panelConfig->collapsed);

  refreshUi();
  connectAll();
}

DopplerTool::~DopplerTool()
{
  delete ui;
}

void
DopplerTool::connectAll()
{

}

void
DopplerTool::refreshUi()
{

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
