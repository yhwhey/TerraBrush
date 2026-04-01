#ifndef ROAD_TOOL_H
#define ROAD_TOOL_H

#include "tool_base.h"
#include "../editor_resources/zone_resource.h"
#include "../misc/hash_utils.h"
#include "../misc/utils.h"

#include <godot_cpp/classes/node3d.hpp>

using namespace godot;

class RoadTool : public ToolBase {
    GDCLASS(RoadTool, ToolBase);

private:
    const float DefaultMaxGradeAngle = 8.0f;
    const float MinGradeAngle = 0.5f;
    const float MaxGradeAngleLimit = 45.0f;
    const float DefaultEdgeFalloff = 0.3f;

    float _maxGradeAngle = DefaultMaxGradeAngle;
    float _edgeFalloff = DefaultEdgeFalloff; // 0.0 = hard edge, 1.0 = full gradient
    int _selectedTextureIndex = -1;

    bool _isFirstPaint = true;
    Vector2 _previousImagePosition = Vector2();
    float _previousHeight = 0.0f;

    std::unordered_set<Ref<ZoneResource>> _sculptedZones = std::unordered_set<Ref<ZoneResource>>();

    void paintSplatmapForPixel(ImageZoneInfo &imageZoneInfo, float strength);

protected:
    static void _bind_methods();

    bool getApplyResolution() const override;
    Ref<Image> getToolCurrentImage(Ref<ZoneResource> zone) override;

public:
    RoadTool();
    ~RoadTool();

    String getToolInfo(TerrainToolType toolType) override;
    bool handleInput(TerrainToolType toolType, Ref<InputEvent> event) override;
    void beginPaint() override;
    void paint(TerrainToolType toolType, Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) override;
    void endPaint() override;

    float getMaxGradeAngle();
    void updateMaxGradeAngle(float value);
    float getEdgeFalloff();
    void updateEdgeFalloff(float value);
    void updateSelectedTextureIndex(int value);
};
#endif
