//
//    ExternalTool.h: description
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
#ifndef EXTERNALTOOL_H
#define EXTERNALTOOL_H

#include <ExternalToolFactory.h>
#include <WFHelpers.h>
#include <QWidget>
#include <QVector>
#include <vector>
#include <ForwarderWidget.h>

namespace Ui {
  class ExternalTool;
}

namespace SigDigger {
  class ExternalToolConfig : public Suscan::Serializable {
  public:
    std::vector<ForwarderWidgetConfig> toolPresets;
    bool collapsed = false;

    // Overriden methods
    void deserialize(Suscan::Object const &conf) override;
    Suscan::Object &&serialize() override;
  };


  class ExternalTool : public ToolWidget
  {
    Q_OBJECT

    Suscan::Analyzer *m_analyzer = nullptr;
    ExternalToolConfig *m_panelConfig = nullptr;
    QVector<ForwarderWidget *> m_forwarderWidgets;

    void addForwarderWidget(ForwarderWidgetConfig const &);

  public:
    explicit ExternalTool(ExternalToolFactory *, UIMediator *, QWidget *parent = nullptr);
    ~ExternalTool() override;

    // Configuration methods
    Suscan::Serializable *allocConfig() override;
    void applyConfig() override;
    bool event(QEvent *) override;

    // Overriden methods
    void setState(int, Suscan::Analyzer *) override;
    void setQth(Suscan::Location const &) override;
    void setColorConfig(ColorConfig const &) override;
    void setTimeStamp(struct timeval const &) override;
    void setProfile(Suscan::Source::Config &) override;

  public slots:
    void onConfigChanged();

  private:
    Ui::ExternalTool *ui;
  };
}

#endif // EXTERNALTOOL_H
