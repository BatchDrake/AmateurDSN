//
//    ChirpCorrector.cpp: Chirp corrector
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
#include "ChirpCorrector.h"
#include <SuWidgetsHelpers.h>

using namespace SigDigger;

SUBOOL
onChirpCorrectorBaseBandData(
    void *privdata,
    suscan_analyzer_t *,
    SUCOMPLEX *samples,
    SUSCOUNT length)
{
  ChirpCorrector *corrector = reinterpret_cast<ChirpCorrector *>(privdata);

  corrector->process(samples, length);

  return SU_TRUE;
}

ChirpCorrector::ChirpCorrector()
{
  su_ncqo_init(&m_ncqo, 0);
}

void
ChirpCorrector::refreshCorrector()
{
  if (m_doNewFreq) {
    SUDOUBLE sampRate = SCAST(SUDOUBLE, m_analyzer->getSampleRate());
    // TODO: Do an atomic xchg
    SUDOUBLE newResetOmega = -SCAST(
          SUDOUBLE,
          SU_NORM2ANG_FREQ(SU_ABS2NORM_FREQ(sampRate, m_desiredResetFreq)));

    m_currOmega += newResetOmega - m_resetOmega;

    m_resetOmega = newResetOmega;
    m_doNewFreq = false;
  }

  if (m_doReset) {
    m_currOmega = m_resetOmega;
    m_doReset = false;
  }

  if (m_doNewRate) {
    SUDOUBLE chirpRatePerSample;
    SUDOUBLE sampRate = SCAST(SUDOUBLE, m_analyzer->getSampleRate());

    // TODO: Rely on an atomic xchg
    m_chirpRate = m_desiredRate;

    chirpRatePerSample = m_chirpRate / sampRate;
    m_deltaOmega =  -SCAST(
          SUDOUBLE,
          SU_NORM2ANG_FREQ(SU_ABS2NORM_FREQ(sampRate, chirpRatePerSample)));

    m_doNewRate = false;
  }
}

void
ChirpCorrector::process(SUCOMPLEX *samples, SUSCOUNT length)
{
  if (m_enabled) {
    SUSCOUNT i;
    SUDOUBLE currOmega, deltaOmega;

    refreshCorrector();

    currOmega  = m_currOmega;
    deltaOmega = m_deltaOmega;

    for (i = 0; i < length; ++i) {
      currOmega += deltaOmega;
      if (currOmega > M_PI)
        currOmega -= 2 * M_PI;
      else if (currOmega < -M_PI)
        currOmega += 2 * M_PI;

      su_ncqo_set_angfreq(&m_ncqo, SU_ASFLOAT(currOmega));
      samples[i] *= su_ncqo_read(&m_ncqo);
    }

    m_currOmega = currOmega;
  }
}

void
ChirpCorrector::ensureCorrector()
{
  if (m_analyzer != nullptr) {
    if (m_enabled && !m_installed) {
      m_doNewFreq = true;
      m_doNewRate = true;
      m_doReset   = true;
      m_analyzer->registerBaseBandFilter(
            onChirpCorrectorBaseBandData,
            this,
            AMATEUR_DSN_CHIRP_CORRECTOR_PRIO);
      m_installed = true;
    }
  }
}

void
ChirpCorrector::setAnalyzer(Suscan::Analyzer *analyzer)
{
  if (analyzer != m_analyzer)
    m_installed = false;

  m_analyzer = analyzer;

  ensureCorrector();
}

void
ChirpCorrector::setEnabled(bool enabled)
{
  m_enabled = enabled;
  ensureCorrector();
}

void
ChirpCorrector::setResetFrequency(SUDOUBLE freq)
{
  m_desiredResetFreq = freq;
  m_doNewFreq = true;
}

void
ChirpCorrector::setChirpRate(SUDOUBLE rate)
{
  m_desiredRate = rate;
  m_doNewRate = true;

}

void
ChirpCorrector::reset()
{
  m_doReset = true;
}

ChirpCorrector::~ChirpCorrector()
{
  // We do not need this for now
}
