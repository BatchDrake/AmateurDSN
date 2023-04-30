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
  Suscan::Object presets;

  LOAD(collapsed);

  toolPresets.clear();

  try {
    presets = conf.getField("presets");

    SU_ATTEMPT(presets.getType() == SUSCAN_OBJECT_TYPE_SET);
    SU_ATTEMPT(presets.length() > 0);

    for (unsigned int i = 0; i < presets.length(); ++i) {
      ForwarderWidgetConfig preset;

      preset.deserialize(presets[i]);

      toolPresets.push_back(preset);
    }
  } catch (Suscan::Exception &) {
    for (unsigned int i = 0; i < 4; ++i) {
      ForwarderWidgetConfig preset;

      preset.title       = "Baudline #" + std::to_string(i + 1);
      preset.programPath = "/usr/bin/baudline";
      preset.arguments   = "-samplerate %SAMPLERATE% -channels 2 -stdin -record -quadrature -format le32f -scaleby %FFTSIZE% -flipcomplex";

      toolPresets.push_back(preset);
    }
  }
}

Suscan::Object &&
ExternalToolConfig::serialize()
{
  Suscan::Object obj(SUSCAN_OBJECT_TYPE_OBJECT);
  Suscan::Object presets(SUSCAN_OBJECT_TYPE_SET);
  unsigned int i;

  obj.setClass("ExternalToolConfig");

  STORE(collapsed);

  obj.setField("presets", presets);

  for (i = 0; i < toolPresets.size(); ++i)
    presets.append(toolPresets[i].serialize());

  return persist(obj);
}

/////////////////////////// Widget implementation //////////////////////////////
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
}

ExternalTool::~ExternalTool()
{
  delete ui;
}

void
ExternalTool::addForwarderWidget(ForwarderWidgetConfig const &conf)
{
  ForwarderWidget *widget = new ForwarderWidget(m_mediator);

  widget->setConfig(conf);
  ui->contentsLayout->addWidget(widget);
  m_forwarderWidgets.append(widget);

  connect(
        widget,
        SIGNAL(configChanged()),
        this,
        SLOT(onConfigChanged()));
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

  for (unsigned int i = 0; i < m_panelConfig->toolPresets.size(); ++i)
    addForwarderWidget(m_panelConfig->toolPresets[i]);
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

void
ExternalTool::onConfigChanged()
{
  m_panelConfig->toolPresets.clear();

  for (auto i = 0; i < m_forwarderWidgets.count(); ++i)
    m_panelConfig->toolPresets.push_back(m_forwarderWidgets[i]->getConfig());
}
