#pragma once
// The 整备 screen painted (render thread, between uigpu::Renderer Begin and End, under armory.cpp's draw lock): a View
// built by armory_logic.h Build on the UI thread, layer by layer, each layer's quads then its texts.
#include "armory_logic.h"
#include "hudtext.h"
#include "ui_gpu.h"
#include "ui_text.h"

namespace crew::armory {
void Paint(uigpu::Painter& paint,uitext::Labels& labels,const View& view,hudtext::Lang lang,unsigned frame) noexcept;
}
