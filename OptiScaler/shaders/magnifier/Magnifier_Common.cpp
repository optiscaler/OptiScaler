#include "pch.h"
#include "Magnifier_Common.h"
#include <menu/menu_overlay_base.h>
#include <menu/input/input_system.h>

bool Magnifier_Common::ShouldRun()
{
    if (!Config::Instance()->MagnifierEnabled.value_or_default())
        return false;

    if ((!Config::Instance()->MagnifierStaticPosX.has_value() ||
         !Config::Instance()->MagnifierStaticPosY.has_value()) &&
        !MenuOverlayBase::IsVisible())
    {
        return false;
    }

    return true;
}

void Magnifier_Common::FilloutStruct(float Width, float Height, InternalMagnifierParams& internalStruct)
{
    internalStruct.ResolutionX = Width;
    internalStruct.ResolutionY = Height;

    bool staticMode =
        Config::Instance()->MagnifierStaticPosX.has_value() && Config::Instance()->MagnifierStaticPosY.has_value();

    if (staticMode)
    {
        const auto percentX = Config::Instance()->MagnifierStaticPosX.value_or(50.f);
        const auto percentY = Config::Instance()->MagnifierStaticPosY.value_or(50.f);

        internalStruct.CursorPosX = (int) (percentX * internalStruct.ResolutionX / 100);
        internalStruct.CursorPosY = (int) (percentY * internalStruct.ResolutionY / 100);
    }
    else
    {
        const auto mouseScreenPos = OptiInput::GetMouseScreenPos();
        internalStruct.CursorPosX = mouseScreenPos.x;
        internalStruct.CursorPosY = mouseScreenPos.y;

        internalStruct.OffsetX = Config::Instance()->MagnifierCursorOffsetX.value_or_default();
        internalStruct.OffsetY = Config::Instance()->MagnifierCursorOffsetY.value_or_default();
    }

    internalStruct.Radius = Config::Instance()->MagnifierSize.value_or_default() * internalStruct.ResolutionY / 100;
    internalStruct.ZoomFactor = Config::Instance()->MagnifierZoomFactor.value_or_default();
    internalStruct.BorderThickness =
        Config::Instance()->MagnifierBorderSize.value_or_default() * internalStruct.ResolutionY / 100;
}