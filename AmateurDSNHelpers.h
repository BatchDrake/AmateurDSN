//
//    AmateurDSNHelpers.h: description
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
#ifndef AMATEURDSNHELPERS_H
#define AMATEURDSNHELPERS_H

#define ADSN_SPEED_OF_LIGHT 299792458. // [m/s]

static inline double
shift2vel(double f0, double shift)
{
  return -ADSN_SPEED_OF_LIGHT / f0 * shift;
}

static inline double
vel2shift(double f0, double vel)
{
  return -f0 / ADSN_SPEED_OF_LIGHT * vel;
}

static inline double
drift2accel(double f0, double drift)
{
  return -ADSN_SPEED_OF_LIGHT / f0 * drift;
}

static inline double
accel2drift(double f0, double accel)
{
  return -f0 / ADSN_SPEED_OF_LIGHT * accel;
}

#endif // AMATEURDSNHELPERS_H
