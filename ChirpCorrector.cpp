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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  define QMutexLocker QMutexLocker<QRecursiveMutex>
#endif

using namespace SigDigger;

SUBOOL
onChirpCorrectorBaseBandData(
    void *privdata,
    suscan_analyzer_t *,
    SUCOMPLEX *samples,
    SUSCOUNT length,
    SUSCOUNT offset)
{
  ChirpCorrector *corrector = reinterpret_cast<ChirpCorrector *>(privdata);

  corrector->process(samples, length, offset);

  return SU_TRUE;
}

ChirpCorrector::ChirpCorrector()
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
  : m_dataExchangeMutex(QMutex::Recursive)
#endif
{
  su_ncqo_init(&m_ncqo, 0);
}

bool
ChirpCorrector::needsRefresh() const
{
  return m_doNewFreq || m_doReset || m_doNewRate;
}

void
ChirpCorrector::refreshCorrector(SUSCOUNT offset)
{
  QMutexLocker locker(&m_dataExchangeMutex);

  if (offset != m_expectedOffset) {
    // Seek found! Adjust the frequency accordingly. We note that
    // at m_expectedOffset the frequency was omega. In our model:
    //
    // omega = omega0 + m(offset - offset0)
    //
    // In which:
    //    omega0  = m_resetOmega
    //    offset0 = m_resetOffset
    //    m       = m_deltaOmega

    SUSDIFF deltaOff = SCAST(SUSDIFF, offset - m_refOffset);
    m_currOmega      = m_refOmega + m_deltaOmega * SCAST(SUDOUBLE, deltaOff);
    m_expectedOffset = offset;
    m_haveCurrOmega = true;
  }

  if (m_doNewFreq) {
    SUDOUBLE sampRate = SCAST(SUDOUBLE, m_analyzer->getSampleRate());
    // TODO: Do an atomic xchg
    SUDOUBLE newResetOmega = -SCAST(
          SUDOUBLE,
          SU_NORM2ANG_FREQ(SU_ABS2NORM_FREQ(sampRate, m_desiredResetFreq)));

    m_currOmega += newResetOmega - m_resetOmega;
    m_haveCurrOmega = true;
    m_resetOmega = newResetOmega;
    m_sampCount  = 0;
    m_doNewFreq  = false;
    m_refOmega   = m_currOmega;
    m_refOffset  = offset;
  }

  if (m_doReset) {
    m_currOmega = m_refOmega = m_resetOmega;
    m_refOffset = offset;
    m_doReset = false;
    m_haveCurrOmega = true;
    m_reportedCurrOmega = m_currOmega;
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

    m_refOmega  = m_currOmega;
    m_refOffset = offset;
    m_doNewRate = false;
  }
}

void
ChirpCorrector::process(SUCOMPLEX *samples, SUSCOUNT length, SUSCOUNT offset)
{
  if (m_enabled) {
    SUSCOUNT i;
    SUDOUBLE currOmega, deltaOmega;

    if (needsRefresh() || offset != m_expectedOffset)
      refreshCorrector(offset);

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

    m_sampCount      += length;
    m_expectedOffset  = length + offset;
    m_currOmega       = currOmega;

    if (m_sampCount > m_sampCountMax) {
      QMutexLocker locker(&m_dataExchangeMutex);
      m_haveCurrOmega = true;
      m_reportedCurrOmega = m_currOmega;
    }
  }
}

void
ChirpCorrector::ensureCorrector()
{
  if (m_analyzer != nullptr) {
    if (m_enabled && !m_installed) {
      m_doNewFreq = true;
      m_doNewRate = true;
      m_sampCountMax = m_analyzer->getSampleRate();
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
  if (analyzer != m_analyzer) {
    m_installed = false;
    m_haveCurrOmega = false;
  }

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
  QMutexLocker locker(&m_dataExchangeMutex);
  m_desiredResetFreq = freq;
  m_doNewFreq = true;
}

void
ChirpCorrector::setChirpRate(SUDOUBLE rate)
{
  QMutexLocker locker(&m_dataExchangeMutex);
  m_desiredRate = rate;
  m_doNewRate = true;

}

SUFLOAT
ChirpCorrector::getCurrentCorrection()
{
  SUDOUBLE currOmega = 0;

  if (m_haveCurrOmega) {
    QMutexLocker locker(&m_dataExchangeMutex);
    currOmega = m_reportedCurrOmega;
  }

  if (m_analyzer == nullptr)
    return 0;

  return SU_NORM2ABS_FREQ(
        m_analyzer->getSampleRate(),
        SU_ANG2NORM_FREQ(SU_ASFLOAT(currOmega)));
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
