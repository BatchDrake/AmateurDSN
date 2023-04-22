//
//    SNRTool.h: description
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
#ifndef SNRTOOL_H
#define SNRTOOL_H

#include <SNRToolFactory.h>
#include <QWidget>

namespace Ui {
  class SNRTool;
}

namespace SigDigger{
  class AnalyzerRequestTracker;

  class SNRToolConfig : public Suscan::Serializable {
  public:
    bool collapsed = false;

    // Overriden methods
    void deserialize(Suscan::Object const &conf) override;
    Suscan::Object &&serialize() override;
  };

  class SNRTool : public ToolWidget
  {
    Q_OBJECT

    // SNR state
    bool m_haveSignalNoise = false;
    bool m_haveNoise = false;
    AnalyzerRequestTracker *m_signalNoiseTracker = nullptr;
    AnalyzerRequestTracker *m_noiseTracker = nullptr;


    qreal m_currentSignalNoiseAlpha = 1;
    qreal m_currentNoiseAlpha       = 1;
    qreal m_currentSignalNoise      = -1;
    qreal m_currentNoise            = -1;

    SNRToolConfig *m_panelConfig = nullptr;

    // In hold mode:
    //    We set the integration time to T_i = MAX(100 ms, 1 / equiv_fs)
    //    We configure the alpha to tau / T_i
    //    For each sample, we update and refresh measurements

    // In single shot mode:
    //    We set the integration time to MAX(tau, equiv_fs)
    //    We wait for a sample (maybe skip the first one?)
    //    After the sample, close the tracker and update

    // In all cases, we display the status

    void openSignalNoiseProbe(bool hold = false);
    void openNoiseProbe(bool hold = false);

    void cancelSignalNoiseProbe();
    void cancelNoiseProbe();

    void refreshUi();

  public:
    explicit SNRTool(SNRToolFactory *, UIMediator *, QWidget *parent = nullptr);
    ~SNRTool() override;

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

  private:
    Ui::SNRTool *ui;
  };

}

#endif // SNRTOOL_H
