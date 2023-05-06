//
//    DopplerTool.h: description
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
#ifndef DOPPLERTOOL_H
#define DOPPLERTOOL_H

#include <DopplerToolFactory.h>
#include <QWidget>
#include <WFHelpers.h>

namespace Ui {
  class DopplerTool;
}

namespace Suscan {
  class AnalyzerRequestTracker;
  class AnalyzerRequest;
}

namespace SigDigger {
  class MainSpectrum;
  class ChirpCorrector;

  class DopplerToolConfig : public Suscan::Serializable {
  public:
    bool   collapsed = false;
    double velocity  = 0;
    double accel     = 0;
    double bias      = 0;
    bool   enabled   = false;

    // Overriden methods
    void deserialize(Suscan::Object const &conf) override;
    Suscan::Object &&serialize() override;
  };

  class DopplerTool : public ToolWidget
  {
    Q_OBJECT

    DopplerToolConfig *m_panelConfig = nullptr;
    Suscan::Analyzer  *m_analyzer    = nullptr;
    MainSpectrum      *m_spectrum    = nullptr;
    ChirpCorrector    *m_corrector   = nullptr;

    // This is what is actually passed to the corrector
    qreal m_currResetFreq = 0;
    qreal m_currRate = 0;
    qreal m_correctedRate = 0;

    void refreshUi();
    void connectAll();
    void applySpectrumState();

    void setFromVelocity(qreal);
    void setFromShift(qreal);

    void setFromAccel(qreal);
    void setFromRate(qreal);

    bool enterChangeState();
    void leaveChangeState(bool);

  public:
    explicit DopplerTool(DopplerToolFactory *, UIMediator *, QWidget *parent = nullptr);
    ~DopplerTool() override;

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
    void onVelChanged();
    void onAccelChanged();

    void onShiftChanged();
    void onRateChanged();
    void onBiasChanged();
    void onReset();
    void onToggleEnabled();
    void onFrequencyChanged();

  private:
    Ui::DopplerTool *ui;
  };

}

#endif // DOPPLERTOOL_H
