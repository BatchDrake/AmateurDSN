//
//    ForwarderWidget.h: description
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
#ifndef FORWARDERWIDGET_H
#define FORWARDERWIDGET_H

#include <QWidget>
#include <Suscan/Library.h>
#include <Suscan/Analyzer.h>

namespace Ui {
  class ForwarderWidget;
}

namespace SigDigger {
  class MainSpectrum;
  class UIMediator;
  class ProcessForwarder;

  class ForwarderWidgetConfig : public Suscan::Serializable {
  public:
    std::string title;
    std::string programPath;
    std::string arguments;

    // Overriden methods
    void deserialize(Suscan::Object const &conf) override;
    Suscan::Object &&serialize() override;
  };

  class ForwarderWidget : public QWidget
  {
    Q_OBJECT

    MainSpectrum     *m_spectrum  = nullptr;
    UIMediator       *m_mediator  = nullptr;
    ProcessForwarder *m_forwarder = nullptr;
    Suscan::Analyzer *m_analyzer  = nullptr;

    NamedChannelSetIterator m_namChan;
    bool              m_haveNamChan = false;

    ForwarderWidgetConfig m_config;

    void refreshUi();
    void connectAll();
    void refreshNamedChannel();
    void applySpectrumState();

  public:
    explicit ForwarderWidget(UIMediator *, QWidget *parent = nullptr);
    ~ForwarderWidget() override;

    void setState(int, Suscan::Analyzer *);
    void setProgramPath(QString const &);
    void setArguments(QString const &);
    void setFrequency(qreal);
    void setBandwidth(qreal);

    void setName(QString const &);
    void mouseDoubleClickEvent(QMouseEvent *) override;
    QString programPath() const;
    QString arguments() const;

    void setConfig(ForwarderWidgetConfig const &);
    ForwarderWidgetConfig const &getConfig() const;

  signals:
    void configChanged();

  public slots:
    void onOpen();
    void onTerminate();
    void onDetach();

    void onBrowse();

    void onAdjustBandwidth();
    void onAdjustFrequency();

    void onForwarderStateChanged(int, QString const &);
    void onConfigChanged();

  private:
    Ui::ForwarderWidget *ui;
  };
}

#endif // FORWARDERWIDGET_H
