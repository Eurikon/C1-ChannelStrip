/*
 * C1-ChannelStrip Plugin - Free modular channel strip for VCV Rack
 *
 * Copyright (c) 2025 Twisted Cable. Licensed under GPL-3.0-or-later.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
    pluginInstance = p;

    // Main modules
    p->addModel(modelChanIn);
    p->addModel(modelShape);
    p->addModel(modelC1EQ);
    p->addModel(modelC1COMP);
    p->addModel(modelChanOut);

    // CV Expanders
    p->addModel(modelChanInCV);
    p->addModel(modelShapeCV);
    p->addModel(modelC1COMPCV);
    p->addModel(modelChanOutCV);
}
