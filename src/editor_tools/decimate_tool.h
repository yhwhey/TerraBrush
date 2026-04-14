#ifndef DECIMATE_TOOL_H
#define DECIMATE_TOOL_H

#include "tool_base.h"
#include "../editor_resources/zone_resource.h"
#include "../misc/hash_utils.h"

#include <map>
#include <tuple>

using namespace godot;

class DecimateTool : public ToolBase{
    GDCLASS(DecimateTool, ToolBase);

private:
    int _facetSize = 4;
    std::unordered_set<Ref<ZoneResource>> _sculptedZones = std::unordered_set<Ref<ZoneResource>>();
    // Cache corner heights across paint calls so repeated strokes during a drag
    // use the original terrain heights, preventing cumulative height drift.
    std::map<std::tuple<int, int, int>, float> _cornerCache;

    void decimate(Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition);

protected:
    static void _bind_methods();

    bool getApplyResolution() const override;
    String getToolInfo(TerrainToolType toolType) override;
    bool handleInput(TerrainToolType toolType, Ref<InputEvent> event) override;
    void beginPaint() override;
    void endPaint() override;
    Ref<Image> getToolCurrentImage(Ref<ZoneResource> zone) override;

public:
    DecimateTool();
    ~DecimateTool();

    void paint(TerrainToolType toolType, Ref<Image> brushImage, int brushSize, float brushStrength, Vector2 imagePosition) override;

    int getFacetSize() const;
    void updateFacetSize(int value);
};
#endif
