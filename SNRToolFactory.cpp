//
//    SNRToolFactory.cpp: Make SNRTool widgets
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

#include "SNRToolFactory.h"
#include "SNRTool.h"

using namespace SigDigger;

const char *
SNRToolFactory::name() const
{
  return "SNRTool";
}

ToolWidget *
SNRToolFactory::make(UIMediator *mediator)
{
  return new SNRTool(this, mediator);
}

std::string
SNRToolFactory::getTitle() const
{
  return "SNR Tool";
}

SNRToolFactory::SNRToolFactory(Suscan::Plugin *plugin) :
  ToolWidgetFactory(plugin)
{

}
