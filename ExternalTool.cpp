//
//    ExternalTool.cpp: description
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
#include "ExternalTool.h"
#include "ui_ExternalTool.h"
#include "ForwarderWidget.h"

using namespace SigDigger;

#define STRINGFY(x) #x
#define STORE(field) obj.set(STRINGFY(field), this->field)
#define LOAD(field) this->field = conf.get(STRINGFY(field), this->field)

//////////////////////////// Widget config /////////////////////////////////////
void
ExternalToolConfig::deserialize(Suscan::Object const &conf)
{
  LOAD(collapsed);
}

Suscan::Object &&
ExternalToolConfig::serialize()
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);

  obj.setClass("ExternalToolConfig");

  STORE(collapsed);

  return persist(obj);
}

ExternalTool::ExternalTool(
    ExternalToolFactory *factory,
    UIMediator *mediator,
    QWidget *parent) :
  ToolWidget(factory, mediator, parent),
  ui(new Ui::ExternalTool)
{
  ui->setupUi(this);

  assertConfig();

  m_mediator = mediator;

  setProperty("collapsed", m_panelConfig->collapsed);

  for (int i = 0; i < 4; ++i)
    addForwarderWidget("External tool #" + QString::number(i + 1));
}

ExternalTool::~ExternalTool()
{
  delete ui;
}

void
ExternalTool::addForwarderWidget(QString const &name)
{
  ForwarderWidget *widget = new ForwarderWidget(m_mediator);

  widget->setName(name);
  ui->contentsLayout->addWidget(widget);
  m_forwarderWidgets.append(widget);
}

// Configuration methods
Suscan::Serializable *
ExternalTool::allocConfig()
{
  return m_panelConfig = new ExternalToolConfig();
}

void
ExternalTool::applyConfig()
{
  setProperty("collapsed", m_panelConfig->collapsed);
}

bool
ExternalTool::event(QEvent *event)
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
ExternalTool::setState(int state, Suscan::Analyzer *analyzer)
{
  for (auto p : m_forwarderWidgets)
    p->setState(state, analyzer);
}

void
ExternalTool::setQth(Suscan::Location const &)
{

}

void
ExternalTool::setColorConfig(ColorConfig const &)
{

}

void
ExternalTool::setTimeStamp(struct timeval const &)
{

}

void
ExternalTool::setProfile(Suscan::Source::Config &)
{

}

