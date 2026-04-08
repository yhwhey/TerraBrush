#include "sculpt_tool.h"
#include "../misc/setting_contants.h"
#include "../misc/zone_utils.h"

#include <functional>
#include <godot_cpp/classes/project_settings.hpp>

using namespace godot;

void SculptTool::_bind_methods() {}

SculptTool::SculptTool() {}

SculptTool::~SculptTool() {}

bool SculptTool::getApplyResolution() const {
    return true;
}

void SculptTool::beginPaint() {
    ToolBase::beginPaint();

    _sculptingMultiplier = ProjectSettings::get_singleton()->get_setting(SettingContants::SculptingMultiplier(), SettingContants::SculptingMultiplierDefaultValue());
    _sculptedZones = std::unordered_set<Ref<ZoneResource>>();
}

void SculptTool::endPaint() {
    ToolBase::endPaint();

    TypedArray<Ref<ZoneResource>> sculptedZonesList = TypedArray<Ref<ZoneResource>>();
    for (Ref<ZoneResource> zone : _sculptedZones) {
        sculptedZonesList.append(zone);
    }
    _terraBrush->updateObjectsHeight(sculptedZonesList);

    _sculptedZones.clear();
    _sculptedZones = std::unordered_set<Ref<ZoneResource>>();
}

Ref<Image> SculptTool::getToolCurrentImage(Ref<ZoneResource> zone) {
    return zone->get_heightMapImage();
}

void SculptTool::paint(TerrainToolType toolType, Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) {
    ToolBase::paint(toolType, brushImage, brushSize, brushStrength, imagePosition);

    switch (toolType) {
        case TerrainToolType::TERRAINTOOLTYPE_TERRAINSMOOTH:
            smoothFromCenter(brushImage, brushSize, brushStrength, imagePosition);
            break;
        case TerrainToolType::TERRAINTOOLTYPE_TERRAINFLATTEN:
            flatten(brushImage, brushSize, brushStrength, imagePosition);
            break;
        default:
            sculpt(toolType, brushImage, brushSize, brushStrength, imagePosition);
            smooth(brushImage, brushSize, 1.0, imagePosition);
            break;
    }

    _terraBrush->get_terrainZones()->updateHeightmaps();
}

void SculptTool::sculpt(TerrainToolType toolType, Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) {
    forEachBrushPixel(brushImage, brushSize, imagePosition, ([&](ImageZoneInfo &imageZoneInfo, float pixelBrushStrength) {
        Color currentPixel = imageZoneInfo.image->get_pixel(imageZoneInfo.zoneInfo.imagePosition.x, imageZoneInfo.zoneInfo.imagePosition.y);
        float newValue = pixelBrushStrength * brushStrength * _sculptingMultiplier;

        if (toolType == TerrainToolType::TERRAINTOOLTYPE_TERRAINADD) {
            newValue = currentPixel.r + newValue;
        } else if (toolType == TerrainToolType::TERRAINTOOLTYPE_TERRAINREMOVE) {
            newValue = currentPixel.r - newValue;
        }

        Color newPixel = Color(newValue, currentPixel.g, currentPixel.b, currentPixel.a);
        imageZoneInfo.image->set_pixel(imageZoneInfo.zoneInfo.imagePosition.x, imageZoneInfo.zoneInfo.imagePosition.y, newPixel);
        _sculptedZones.insert(imageZoneInfo.zone);
    }));
}

void SculptTool::flatten(Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) {
    Color smoothValue = Color(1, 1, 1, 0); // Transparent color
    int numberOfSamples = 0;

    forEachBrushPixel(brushImage, brushSize, imagePosition, ([&](ImageZoneInfo &imageZoneInfo, float pixelBrushStrength) {
        Color currentPixel = imageZoneInfo.image->get_pixel(imageZoneInfo.zoneInfo.imagePosition.x, imageZoneInfo.zoneInfo.imagePosition.y);

        smoothValue += currentPixel;
        numberOfSamples += 1;
    }));

    smoothValue = smoothValue / numberOfSamples;

    forEachBrushPixel(brushImage, brushSize, imagePosition, ([&](ImageZoneInfo &imageZoneInfo, float pixelBrushStrength) {
        Color currentPixel = imageZoneInfo.image->get_pixel(imageZoneInfo.zoneInfo.imagePosition.x, imageZoneInfo.zoneInfo.imagePosition.y);
        Color newValue = Color(
            Math::lerp(currentPixel.r, smoothValue.r, pixelBrushStrength * brushStrength),
            currentPixel.g,
            currentPixel.b,
            currentPixel.a
        );

        imageZoneInfo.image->set_pixel(imageZoneInfo.zoneInfo.imagePosition.x, imageZoneInfo.zoneInfo.imagePosition.y, newValue);
        _sculptedZones.insert(imageZoneInfo.zone);
    }));
}

void SculptTool::smooth(Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) {
    forEachBrushPixel(brushImage, brushSize, imagePosition, ([&](ImageZoneInfo &imageZoneInfo, float pixelBrushStrength) {
        std::vector<float> directions = std::vector<float>();

        ImageZoneInfo neighbourImageZoneInfo = getImageZoneInfoForPosition(imageZoneInfo.zoneInfo, -1, 0, true);
        if (!neighbourImageZoneInfo.zone.is_null()) {
            directions.push_back(neighbourImageZoneInfo.image->get_pixel(neighbourImageZoneInfo.zoneInfo.imagePosition.x, neighbourImageZoneInfo.zoneInfo.imagePosition.y).r);
        }

        neighbourImageZoneInfo = getImageZoneInfoForPosition(imageZoneInfo.zoneInfo, 1, 0, true);
        if (!neighbourImageZoneInfo.zone.is_null()) {
            directions.push_back(neighbourImageZoneInfo.image->get_pixel(neighbourImageZoneInfo.zoneInfo.imagePosition.x, neighbourImageZoneInfo.zoneInfo.imagePosition.y).r);
        }

        neighbourImageZoneInfo = getImageZoneInfoForPosition(imageZoneInfo.zoneInfo, 0, -1, true);
        if (!neighbourImageZoneInfo.zone.is_null()) {
            directions.push_back(neighbourImageZoneInfo.image->get_pixel(neighbourImageZoneInfo.zoneInfo.imagePosition.x, neighbourImageZoneInfo.zoneInfo.imagePosition.y).r);
        }

        neighbourImageZoneInfo = getImageZoneInfoForPosition(imageZoneInfo.zoneInfo, 0, 1, true);
        if (!neighbourImageZoneInfo.zone.is_null()) {
            directions.push_back(neighbourImageZoneInfo.image->get_pixel(neighbourImageZoneInfo.zoneInfo.imagePosition.x, neighbourImageZoneInfo.zoneInfo.imagePosition.y).r);
        }

        Color currentPixel = imageZoneInfo.image->get_pixel(imageZoneInfo.zoneInfo.imagePosition.x, imageZoneInfo.zoneInfo.imagePosition.y);
        directions.push_back(currentPixel.r);

        float average = 0;
        for (float directionValue : directions) {
            average += directionValue;
        }
        average /= directions.size();

        float resultValue = Math::lerp(currentPixel.r, average, pixelBrushStrength * brushStrength);

        Color newPixel = Color(resultValue, currentPixel.g, currentPixel.b, currentPixel.a);
        imageZoneInfo.image->set_pixel(imageZoneInfo.zoneInfo.imagePosition.x, imageZoneInfo.zoneInfo.imagePosition.y, newPixel);
        _sculptedZones.insert(imageZoneInfo.zone);
    }));
}

void SculptTool::smoothFromCenter(Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) {
    int zonesSize = _terraBrush->get_zonesSize();
    int resolution = _terraBrush->get_resolution();
    ZoneInfo centerZoneInfo = ZoneUtils::getPixelToZoneInfo(imagePosition.x, imagePosition.y, zonesSize, resolution);

    int sampleRadius = MAX(brushSize / 10, 2);

    // Fit a plane to the center area: h(dx,dy) = a + b*dx + c*dy
    // With symmetric sampling, sum(dx)=0, sum(dy)=0, sum(dx*dy)=0, so:
    //   a = mean(h), b = sum(h*dx)/sum(dx^2), c = sum(h*dy)/sum(dy^2)
    double sumH = 0, sumHdx = 0, sumHdy = 0, sumDx2 = 0, sumDy2 = 0;
    int count = 0;

    for (int dx = -sampleRadius; dx <= sampleRadius; dx++) {
        for (int dy = -sampleRadius; dy <= sampleRadius; dy++) {
            ImageZoneInfo info = getImageZoneInfoForPosition(centerZoneInfo, dx, dy, true);
            if (!info.zone.is_null()) {
                float h = info.image->get_pixel(info.zoneInfo.imagePosition.x, info.zoneInfo.imagePosition.y).r;
                sumH += h;
                sumHdx += h * dx;
                sumHdy += h * dy;
                sumDx2 += (double)dx * dx;
                sumDy2 += (double)dy * dy;
                count++;
            }
        }
    }

    if (count == 0) {
        return;
    }

    float planeA = (float)(sumH / count);
    float planeB = sumDx2 > 0 ? (float)(sumHdx / sumDx2) : 0.0f;
    float planeC = sumDy2 > 0 ? (float)(sumHdy / sumDy2) : 0.0f;

    // Get image size to compute global pixel coordinates across zones
    int imageSize = zonesSize;
    {
        ImageZoneInfo centerInfo = getImageZoneInfoForPosition(centerZoneInfo, 0, 0, true);
        if (!centerInfo.zone.is_null() && !centerInfo.image.is_null()) {
            imageSize = centerInfo.image->get_width();
        }
    }

    int centerGlobalX = centerZoneInfo.zonePosition.x * imageSize + centerZoneInfo.imagePosition.x;
    int centerGlobalY = centerZoneInfo.zonePosition.y * imageSize + centerZoneInfo.imagePosition.y;

    // Apply across the full brush: lerp each pixel toward the fitted plane
    forEachBrushPixel(brushImage, brushSize, imagePosition, ([&](ImageZoneInfo &imageZoneInfo, float pixelBrushStrength) {
        int pixelGlobalX = imageZoneInfo.zoneInfo.zonePosition.x * imageSize + imageZoneInfo.zoneInfo.imagePosition.x;
        int pixelGlobalY = imageZoneInfo.zoneInfo.zonePosition.y * imageSize + imageZoneInfo.zoneInfo.imagePosition.y;
        int dx = pixelGlobalX - centerGlobalX;
        int dy = pixelGlobalY - centerGlobalY;

        float planeHeight = planeA + planeB * dx + planeC * dy;

        Color currentPixel = imageZoneInfo.image->get_pixel(imageZoneInfo.zoneInfo.imagePosition.x, imageZoneInfo.zoneInfo.imagePosition.y);
        float newValue = Math::lerp(currentPixel.r, planeHeight, pixelBrushStrength * brushStrength);

        imageZoneInfo.image->set_pixel(imageZoneInfo.zoneInfo.imagePosition.x, imageZoneInfo.zoneInfo.imagePosition.y, Color(newValue, currentPixel.g, currentPixel.b, currentPixel.a));
        _sculptedZones.insert(imageZoneInfo.zone);
    }));
}