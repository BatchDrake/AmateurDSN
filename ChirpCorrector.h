//
//    ChirpCorrector.h: Chirp corrector
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
#ifndef CHIRPCORRECTOR_H
#define CHIRPCORRECTOR_H

#include <Suscan/Library.h>
#include <Suscan/Analyzer.h>

#define AMATEUR_DSN_CHIRP_CORRECTOR_PRIO -0x1000

SUBOOL onChirpCorrectorBaseBandData(
    void *privdata,
    suscan_analyzer_t *,
    SUCOMPLEX *samples,
    SUSCOUNT length);

namespace SigDigger {
  class ChirpCorrector
  {
    Suscan::Analyzer *m_analyzer = nullptr;
    SUDOUBLE  m_chirpRate = 0;
    SUDOUBLE  m_deltaOmega = 0;
    SUDOUBLE  m_currOmega = 0;
    SUSCOUNT  m_sampCount = 0;

    bool      m_doNewRate = false;
    bool      m_doReset = false;

    SUDOUBLE  m_desiredRate = 0;
    bool      m_enabled = false;
    bool      m_installed = false;
    su_ncqo_t m_ncqo;

    void ensureCorrector();
    void refreshCorrector();
    void process(SUCOMPLEX *samples, SUSCOUNT length);

    friend SUBOOL
    ::onChirpCorrectorBaseBandData(
        void *privdata,
        suscan_analyzer_t *,
        SUCOMPLEX *samples,
        SUSCOUNT length);

  public:
    ChirpCorrector();
    ~ChirpCorrector();

    void setAnalyzer(Suscan::Analyzer *);
    void setEnabled(bool);
    void setChirpRate(SUDOUBLE rate);
    void reset();
  };
}

#endif // CHIRPCORRECTOR_H
