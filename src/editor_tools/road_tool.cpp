#include "road_tool.h"
#include "../misc/zone_utils.h"
#include "../misc/zone_info.h"

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_key.hpp>

using namespace godot;

void RoadTool::_bind_methods() {}

RoadTool::RoadTool() {}

RoadTool::~RoadTool() {}

bool RoadTool::getApplyResolution() const {
    return true;
}

Ref<Image> RoadTool::getToolCurrentImage(Ref<ZoneResource> zone) {
    return zone->get_heightMapImage();
}

String RoadTool::getToolInfo(TerrainToolType toolType) {
    String info = "Max grade: " + String::num_real(_maxGradeAngle) + " deg";
    info += "\nEdge falloff: " + String::num(_edgeFalloff * 100, 0) + "%";
    info += "\nCTRL + scroll: grade | ALT + scroll: falloff";
    if (_selectedTextureIndex < 0) {
        info += "\nNo texture selected";
    }
    return info;
}

bool RoadTool::handleInput(TerrainToolType toolType, Ref<InputEvent> event) {
    if (Input::get_singleton()->is_key_pressed(Key::KEY_CTRL)) {
        float incrementValue = 0;

        if (Object::cast_to<InputEventMouseButton>(event.ptr()) != nullptr) {
            Ref<InputEventMouseButton> inputMouseButton = Object::cast_to<InputEventMouseButton>(event.ptr());
            if (inputMouseButton->get_button_index() == MouseButton::MOUSE_BUTTON_WHEEL_UP) {
                incrementValue = 1;
            } else if (inputMouseButton->get_button_index() == MouseButton::MOUSE_BUTTON_WHEEL_DOWN) {
                incrementValue = -1;
            }
        }

        if (Object::cast_to<InputEventKey>(event.ptr()) != nullptr) {
            Ref<InputEventKey> inputEvent = Object::cast_to<InputEventKey>(event.ptr());
            if (inputEvent->get_keycode() == Key::KEY_EQUAL) {
                incrementValue = 1;
            } else if (inputEvent->get_keycode() == Key::KEY_MINUS) {
                incrementValue = -1;
            }
        }

        if (incrementValue != 0) {
            updateMaxGradeAngle(_maxGradeAngle + incrementValue);
            return true;
        }
    }

    if (Input::get_singleton()->is_key_pressed(Key::KEY_ALT)) {
        float incrementValue = 0;

        if (Object::cast_to<InputEventMouseButton>(event.ptr()) != nullptr) {
            Ref<InputEventMouseButton> inputMouseButton = Object::cast_to<InputEventMouseButton>(event.ptr());
            if (inputMouseButton->get_button_index() == MouseButton::MOUSE_BUTTON_WHEEL_UP) {
                incrementValue = 0.05f;
            } else if (inputMouseButton->get_button_index() == MouseButton::MOUSE_BUTTON_WHEEL_DOWN) {
                incrementValue = -0.05f;
            }
        }

        if (incrementValue != 0) {
            updateEdgeFalloff(_edgeFalloff + incrementValue);
            return true;
        }
    }

    return ToolBase::handleInput(toolType, event);
}

void RoadTool::beginPaint() {
    ToolBase::beginPaint();

    _isFirstPaint = true;
    _previousImagePosition = Vector2();
    _previousHeight = 0.0f;
    _sculptedZones = std::unordered_set<Ref<ZoneResource>>();
}

void RoadTool::paint(TerrainToolType toolType, Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) {
    int zonesSize = _terraBrush->get_zonesSize();
    int resolution = _terraBrush->get_resolution();

    // On first paint, read center height from the (untouched) terrain
    if (_isFirstPaint) {
        ZoneInfo centerZoneInfo = ZoneUtils::getPixelToZoneInfo(imagePosition.x, imagePosition.y, zonesSize, resolution);
        ImageZoneInfo centerImageZoneInfo = getImageZoneInfoForPosition(centerZoneInfo, 0, 0, true);

        if (centerImageZoneInfo.zone.is_null() || centerImageZoneInfo.image.is_null()) {
            return;
        }

        _previousHeight = centerImageZoneInfo.image->get_pixel(
            centerImageZoneInfo.zoneInfo.imagePosition.x,
            centerImageZoneInfo.zoneInfo.imagePosition.y
        ).r;
        _previousImagePosition = imagePosition;
        _isFirstPaint = false;
    }

    // Compute grade-limited target height
    float distance = _previousImagePosition.distance_to(imagePosition);
    float targetHeight;

    if (distance < 0.001f) {
        targetHeight = _previousHeight;
    } else {
        // Sample terrain at the LEADING EDGE of the brush (ahead in the movement
        // direction). The center pixel is likely already painted from previous
        // strokes, which would hide descents. The leading edge is untouched.
        Vector2 moveDir = (imagePosition - _previousImagePosition) / distance;
        Vector2 samplePos = imagePosition + moveDir * (brushSize / 2.0f);

        ZoneInfo sampleZoneInfo = ZoneUtils::getPixelToZoneInfo(samplePos.x, samplePos.y, zonesSize, resolution);
        ImageZoneInfo sampleImageZoneInfo = getImageZoneInfoForPosition(sampleZoneInfo, 0, 0, true);

        float aheadHeight = _previousHeight; // fallback
        if (!sampleImageZoneInfo.zone.is_null() && !sampleImageZoneInfo.image.is_null()) {
            aheadHeight = sampleImageZoneInfo.image->get_pixel(
                sampleImageZoneInfo.zoneInfo.imagePosition.x,
                sampleImageZoneInfo.zoneInfo.imagePosition.y
            ).r;
        }

        float maxDeltaH = distance * (float)Math::tan(Math::deg_to_rad((double)_maxGradeAngle));
        float delta = aheadHeight - _previousHeight;

        if (Math::abs(delta) > maxDeltaH) {
            targetHeight = _previousHeight + Math::sign(delta) * maxDeltaH;
        } else {
            targetHeight = aheadHeight;
        }
    }

    // Store previous position for segment projection
    Vector2 prevPos = _previousImagePosition;
    float prevHeight = _previousHeight;
    Vector2 segDir = imagePosition - prevPos;
    float segLenSq = segDir.dot(segDir);

    int numberOfSplatmaps = 0;
    if (_selectedTextureIndex >= 0 && !_terraBrush->get_textureSets().is_null()) {
        numberOfSplatmaps = (int)Math::ceil(_terraBrush->get_textureSets()->get_textureSets().size() / 4.0);
    }

    float brushRadius = brushSize / 2.0f;

    forEachBrushPixel(brushImage, brushSize, imagePosition, ([&](ImageZoneInfo &imageZoneInfo, float pixelBrushStrength) {
        // Get absolute pixel position for segment projection
        Vector2i absolutePosition = (imageZoneInfo.zoneInfo.imagePosition * resolution)
            + (imageZoneInfo.zoneInfo.zonePosition * (zonesSize - 1));
        Vector2 absPos = Vector2(absolutePosition.x, absolutePosition.y);

        // Project pixel onto segment [prevPos → imagePosition]
        float t = 0.0f;
        if (segLenSq > 0.001f) {
            t = Math::clamp((absPos - prevPos).dot(segDir) / segLenSq, 0.0f, 1.0f);
        }

        // Interpolate graded height along segment
        float lerpedTargetH = Math::lerp(prevHeight, targetHeight, t);

        // Edge falloff: smoothstep based on distance from brush center
        float falloff = 1.0f;
        if (_edgeFalloff > 0.001f && brushRadius > 0.001f) {
            float distFromCenter = absPos.distance_to(imagePosition);
            float normalizedDist = distFromCenter / brushRadius;
            float inner = 1.0f - _edgeFalloff;
            if (normalizedDist > inner) {
                float ft = Math::clamp((normalizedDist - inner) / _edgeFalloff, 0.0f, 1.0f);
                falloff = 1.0f - (ft * ft * (3.0f - 2.0f * ft)); // smoothstep
            }
        }

        // Write heightmap
        float combinedStrength = pixelBrushStrength * brushStrength * falloff;
        Color currentPixel = imageZoneInfo.image->get_pixel(
            imageZoneInfo.zoneInfo.imagePosition.x,
            imageZoneInfo.zoneInfo.imagePosition.y
        );
        float newH = Math::lerp(currentPixel.r, lerpedTargetH, combinedStrength);
        imageZoneInfo.image->set_pixel(
            imageZoneInfo.zoneInfo.imagePosition.x,
            imageZoneInfo.zoneInfo.imagePosition.y,
            Color(newH, currentPixel.g, currentPixel.b, currentPixel.a)
        );
        _sculptedZones.insert(imageZoneInfo.zone);

        // Paint splatmap
        if (_selectedTextureIndex >= 0 && numberOfSplatmaps > 0) {
            paintSplatmapForPixel(imageZoneInfo, combinedStrength);
        }
    }));

    _previousImagePosition = imagePosition;
    _previousHeight = targetHeight;

    _terraBrush->get_terrainZones()->updateHeightmaps();
    if (_selectedTextureIndex >= 0) {
        _terraBrush->get_terrainZones()->updateSplatmapsTextures();
    }
}

void RoadTool::paintSplatmapForPixel(ImageZoneInfo &imageZoneInfo, float strength) {
    int zonesSize = _terraBrush->get_zonesSize();
    int resolution = _terraBrush->get_resolution();
    int numberOfSplatmaps = (int)Math::ceil(_terraBrush->get_textureSets()->get_textureSets().size() / 4.0);

    int splatmapIndex = _selectedTextureIndex / 4;
    int colorChannel = _selectedTextureIndex % 4;

    // Remap heightmap coords to splatmap coords (splatmaps are always full zoneSize)
    int splatX = imageZoneInfo.zoneInfo.imagePosition.x;
    int splatY = imageZoneInfo.zoneInfo.imagePosition.y;

    if (resolution > 1) {
        int hmSize = ZoneUtils::getImageSizeForResolution(zonesSize, resolution);
        splatX = (int)Math::round(Math::remap((float)splatX, 0.0f, (float)(hmSize - 1), 0.0f, (float)(zonesSize - 1)));
        splatY = (int)Math::round(Math::remap((float)splatY, 0.0f, (float)(hmSize - 1), 0.0f, (float)(zonesSize - 1)));
    }
    splatX = Math::clamp(splatX, 0, zonesSize - 1);
    splatY = Math::clamp(splatY, 0, zonesSize - 1);

    Color transparentColor = Color(1, 1, 1, 0);

    for (int i = 0; i < numberOfSplatmaps; i++) {
        Color splatmapColor = transparentColor;

        if (i != splatmapIndex) {
            splatmapColor = Color(0, 0, 0, 0);
        } else if (colorChannel == 0) {
            splatmapColor = Color(1, 0, 0, 0);
        } else if (colorChannel == 1) {
            splatmapColor = Color(0, 1, 0, 0);
        } else if (colorChannel == 2) {
            splatmapColor = Color(0, 0, 1, 0);
        } else if (colorChannel == 3) {
            splatmapColor = Color(0, 0, 0, 1);
        }

        Ref<Image> currentSplatmapImage = imageZoneInfo.zone->get_splatmapsImage()[i];
        _terraBrush->get_terrainZones()->addDirtyImage(currentSplatmapImage);
        addImageToUndo(currentSplatmapImage);

        Color currentPixel = currentSplatmapImage->get_pixel(splatX, splatY);
        Color newValue = Color(
            Math::lerp(currentPixel.r, splatmapColor.r, strength),
            Math::lerp(currentPixel.g, splatmapColor.g, strength),
            Math::lerp(currentPixel.b, splatmapColor.b, strength),
            Math::lerp(currentPixel.a, splatmapColor.a, strength)
        );
        currentSplatmapImage->set_pixel(splatX, splatY, newValue);
    }
}

void RoadTool::endPaint() {
    ToolBase::endPaint();

    TypedArray<Ref<ZoneResource>> sculptedZonesList = TypedArray<Ref<ZoneResource>>();
    for (Ref<ZoneResource> zone : _sculptedZones) {
        sculptedZonesList.append(zone);
    }
    _terraBrush->updateObjectsHeight(sculptedZonesList);

    _sculptedZones.clear();
    _sculptedZones = std::unordered_set<Ref<ZoneResource>>();
}

float RoadTool::getMaxGradeAngle() {
    return _maxGradeAngle;
}

void RoadTool::updateMaxGradeAngle(float value) {
    _maxGradeAngle = Math::clamp(value, MinGradeAngle, MaxGradeAngleLimit);
}

float RoadTool::getEdgeFalloff() {
    return _edgeFalloff;
}

void RoadTool::updateEdgeFalloff(float value) {
    _edgeFalloff = Math::clamp(value, 0.0f, 1.0f);
}

void RoadTool::updateSelectedTextureIndex(int value) {
    _selectedTextureIndex = value;
}
