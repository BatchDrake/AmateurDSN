//
//    Registration.cpp: Register the ZeroMQ forwarder
//    Copyright (C) 2022 Gonzalo José Carracedo Carballal
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

#include <Suscan/Plugin.h>
#include <Suscan/Library.h>
#include <QCoreApplication>
#include <SNRToolFactory.h>
#include <ExternalToolFactory.h>
#include <DopplerToolFactory.h>
#include <DriftToolFactory.h>

SUSCAN_PLUGIN("AmateurDSN", "AmateurDSN Toolkit");
SUSCAN_PLUGIN_VERSION(0, 1, 0);
SUSCAN_PLUGIN_API_VERSION(0, 3, 0);

bool
plugin_load(Suscan::Plugin *plugin)
{
  Suscan::Singleton *sus = Suscan::Singleton::get_instance();

  if (!sus->registerToolWidgetFactory(
        new SigDigger::SNRToolFactory(plugin)))
    return false;

  if (!sus->registerToolWidgetFactory(
        new SigDigger::DopplerToolFactory(plugin)))
    return false;

  if (!sus->registerToolWidgetFactory(
        new SigDigger::DriftToolFactory(plugin)))
    return false;

  if (!sus->registerToolWidgetFactory(
        new SigDigger::ExternalToolFactory(plugin)))
    return false;

  return true;
}
