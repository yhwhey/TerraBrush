#include "decimate_tool.h"

#include <functional>

#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input.hpp>

using namespace godot;

void DecimateTool::_bind_methods() {}

DecimateTool::DecimateTool() {}

DecimateTool::~DecimateTool() {}

bool DecimateTool::getApplyResolution() const {
    return true;
}

String DecimateTool::getToolInfo(TerrainToolType toolType) {
    String initialValue = ToolBase::getToolInfo(toolType);
    return (initialValue + "\nFacet size : " + String::num_int64(_facetSize)).strip_edges();
}

bool DecimateTool::handleInput(TerrainToolType toolType, Ref<InputEvent> event) {
    if (Input::get_singleton()->is_key_pressed(Key::KEY_CTRL)) {
        int incrementValue = 0;
        if (Object::cast_to<InputEventMouseButton>(event.ptr()) != nullptr) {
            Ref<InputEventMouseButton> inputMouseButton = Object::cast_to<InputEventMouseButton>(event.ptr());
            if (inputMouseButton->get_button_index() == MouseButton::MOUSE_BUTTON_WHEEL_UP) {
                incrementValue = 1;
            } else if (inputMouseButton->get_button_index() == MouseButton::MOUSE_BUTTON_WHEEL_DOWN) {
                incrementValue = -1;
            }
        }

        if (incrementValue != 0) {
            updateFacetSize(_facetSize + incrementValue);
            return true;
        }
    }

    return ToolBase::handleInput(toolType, event);
}

void DecimateTool::beginPaint() {
    ToolBase::beginPaint();

    _sculptedZones = std::unordered_set<Ref<ZoneResource>>();
    _cornerCache.clear();
}

void DecimateTool::endPaint() {
    ToolBase::endPaint();

    TypedArray<Ref<ZoneResource>> sculptedZonesList = TypedArray<Ref<ZoneResource>>();
    for (Ref<ZoneResource> zone : _sculptedZones) {
        sculptedZonesList.append(zone);
    }
    _terraBrush->updateObjectsHeight(sculptedZonesList);

    _sculptedZones.clear();
    _sculptedZones = std::unordered_set<Ref<ZoneResource>>();
}

Ref<Image> DecimateTool::getToolCurrentImage(Ref<ZoneResource> zone) {
    return zone->get_heightMapImage();
}

void DecimateTool::paint(TerrainToolType toolType, Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) {
    ToolBase::paint(toolType, brushImage, brushSize, brushStrength, imagePosition);

    decimate(brushImage, brushSize, brushStrength, imagePosition);

    _terraBrush->get_terrainZones()->updateHeightmaps(_terraBrush->get_zonesSize());
}

void DecimateTool::decimate(Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) {
    // _cornerCache persists across paint() calls within a single drag stroke
    // (cleared in beginPaint/endPaint). This ensures repeated strokes sample
    // from the original terrain heights, preventing cumulative height drift.

    auto getCornerHeight = [&](Ref<Image> image, int zoneKey, int gridX, int gridY) -> float {
        auto key = std::make_tuple(zoneKey, gridX, gridY);
        auto it = _cornerCache.find(key);
        if (it != _cornerCache.end()) {
            return it->second;
        }

        int imgW = image->get_width();
        int imgH = image->get_height();

        int sampleX = CLAMP(gridX, 0, imgW - 1);
        int sampleY = CLAMP(gridY, 0, imgH - 1);

        float height = image->get_pixel(sampleX, sampleY).r;
        _cornerCache[key] = height;
        return height;
    };

    // For each pixel, compute the height of the flat triangular facet it falls on.
    // The coarse grid has corners at multiples of _facetSize. Each quad is split into
    // two triangles along the B→C diagonal. Heights at the corners are sampled from
    // the original terrain and cached for the entire drag stroke.
    forEachBrushPixel(brushImage, brushSize, imagePosition, ([&](ImageZoneInfo &imageZoneInfo, float pixelBrushStrength) {
        if (pixelBrushStrength <= 0.0f) {
            return;
        }

        int px = imageZoneInfo.zoneInfo.imagePosition.x;
        int py = imageZoneInfo.zoneInfo.imagePosition.y;
        int zoneKey = imageZoneInfo.zoneInfo.zoneKey;

        // Coarse grid cell
        int cellX = px / _facetSize;
        int cellY = py / _facetSize;
        int cx = cellX * _facetSize;
        int cy = cellY * _facetSize;

        // Heights at the 4 corners of this cell
        float hA = getCornerHeight(imageZoneInfo.image, zoneKey, cx, cy);
        float hB = getCornerHeight(imageZoneInfo.image, zoneKey, cx + _facetSize, cy);
        float hC = getCornerHeight(imageZoneInfo.image, zoneKey, cx, cy + _facetSize);
        float hD = getCornerHeight(imageZoneInfo.image, zoneKey, cx + _facetSize, cy + _facetSize);

        // Local coordinates within cell [0, 1]
        float u = (float)(px - cx) / _facetSize;
        float v = (float)(py - cy) / _facetSize;

        // Split each cell into two triangles along the B(1,0)→C(0,1) diagonal,
        // matching the clipmap mesh topology which splits quads the same way.
        // Aligning diagonals prevents sawtooth artifacts from mesh triangles
        // straddling two different facet planes.
        float facetHeight;
        if (u + v <= 1.0f) {
            // Triangle containing A(0,0): vertices A, B, C
            facetHeight = hA + u * (hB - hA) + v * (hC - hA);
        } else {
            // Triangle containing D(1,1): vertices B, D, C
            facetHeight = hD + (1.0f - u) * (hC - hD) + (1.0f - v) * (hB - hD);
        }

        Color currentPixel = imageZoneInfo.image->get_pixel(px, py);
        float newHeight = Math::lerp(currentPixel.r, facetHeight, pixelBrushStrength * brushStrength);

        imageZoneInfo.image->set_pixel(px, py, Color(newHeight, currentPixel.g, currentPixel.b, currentPixel.a));
        _sculptedZones.insert(imageZoneInfo.zone);
    }));
}

int DecimateTool::getFacetSize() const {
    return _facetSize;
}

void DecimateTool::updateFacetSize(int value) {
    _facetSize = CLAMP(value, 2, 16);
}
