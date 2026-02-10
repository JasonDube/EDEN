#pragma once

#include <eden/Terrain.hpp>
#include <eden/Camera.hpp>
#include <eden/SkyParameters.hpp>
#include <eden/ICharacterController.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <set>

namespace eden {

class SceneObject;
class AINode;
class AIPath;
class ZoneSystem;
enum class AINodeType;
enum class ZoneType : uint8_t;
enum class ResourceType : uint8_t;

// Callback types for UI actions
using SpeedChangedCallback = std::function<void(float)>;
using SkyChangedCallback = std::function<void(const SkyParameters&)>;
using ClearSelectionCallback = std::function<void()>;
using MoveSelectionCallback = std::function<void(const glm::vec3&)>;
using TiltSelectionCallback = std::function<void(float, float)>;
using ImportModelCallback = std::function<void(const std::string&)>;
using BrowseModelCallback = std::function<void()>;  // Opens file dialog to import model
using SelectObjectCallback = std::function<void(int)>;
using MultiSelectObjectCallback = std::function<void(const std::set<int>&)>;
using DeleteObjectCallback = std::function<void(int)>;
using GroupObjectsCallback = std::function<void(const std::set<int>&, const std::string&)>;
using UngroupObjectsCallback = std::function<void(int groupIndex)>;
using BulletCollisionChangedCallback = std::function<void(SceneObject*)>;
using ObjectTransformChangedCallback = std::function<void(SceneObject*)>;
using FreezeTransformCallback = std::function<void(SceneObject*)>;  // Bake transform into mesh vertices
using ApplyPathCallback = std::function<void()>;
using ClearPathCallback = std::function<void()>;
using UndoPathPointCallback = std::function<void()>;
using CreateTubeCallback = std::function<void(float radius, int segments, const glm::vec3& color)>;
using WaterChangedCallback = std::function<void(float level, float amplitude, float frequency, bool visible)>;
using FileNewCallback = std::function<void()>;
using NewTestLevelCallback = std::function<void()>;
using NewSpaceLevelCallback = std::function<void()>;
using FileOpenCallback = std::function<void()>;
using FileSaveCallback = std::function<void()>;
using FileExitCallback = std::function<void()>;
using ExportTerrainCallback = std::function<void()>;
using AddSpawnCallback = std::function<void()>;
using AddCylinderCallback = std::function<void()>;
using AddCubeCallback = std::function<void(float size)>;
using AddDoorCallback = std::function<void()>;
using RunGameCallback = std::function<void()>;

// Behavior script loading callbacks
using LoadBehaviorScriptCallback = std::function<void(SceneObject* target)>;
using ListBotScriptsCallback = std::function<std::vector<std::string>(const std::string& botName)>;
using LoadBotScriptCallback = std::function<void(SceneObject* target, const std::string& scriptName)>;
using SaveBotScriptCallback = std::function<void(SceneObject* target, const std::string& behaviorName)>;

// Grove script editor callbacks
using GroveRunCallback = std::function<void(const std::string& source)>;
using GroveOpenCallback = std::function<void()>;
using GroveSaveCallback = std::function<void(const std::string& source, const std::string& currentPath)>;
using GroveSaveAsCallback = std::function<void(const std::string& source)>;
using GroveFileListCallback = std::function<std::vector<std::string>()>;

// AI Node callbacks
using ToggleAIPlacementCallback = std::function<void(bool enabled, int nodeType)>;
using SelectAINodeCallback = std::function<void(int index)>;
using DeleteAINodeCallback = std::function<void(int index)>;
using AINodePropertyChangedCallback = std::function<void()>;
using GenerateAINodesCallback = std::function<void(int pattern, int count, float radius)>;
using BeginAIConnectionCallback = std::function<void()>;
using ConnectAINodesCallback = std::function<void(int fromIndex, int toIndex)>;
using DisconnectAINodesCallback = std::function<void(int fromIndex, int toIndex)>;
using ConnectAllGraphNodesCallback = std::function<void()>;
using CreateTestEconomyCallback = std::function<void()>;

// Path callbacks
using CreatePathFromNodesCallback = std::function<void(const std::string& name, const std::vector<int>& nodeIndices)>;
using DeletePathCallback = std::function<void(int index)>;
using SelectPathCallback = std::function<void(int index)>;
using PathPropertyChangedCallback = std::function<void()>;

// Script callbacks
using ScriptAddedCallback = std::function<void(int objectIndex, const std::string& scriptName)>;
using ScriptRemovedCallback = std::function<void(int objectIndex, const std::string& scriptName)>;

// Terrain info for display
struct TerrainInfo {
    int chunkCountX = 32;
    int chunkCountZ = 32;
    int chunkResolution = 64;
    float tileSize = 2.0f;
    float heightScale = 200.0f;

    // Computed values
    float chunkSizeMeters() const { return (chunkResolution - 1) * tileSize; }
    float totalSizeMeters() const { return chunkCountX * chunkSizeMeters(); }
    float totalSizeMiles() const { return totalSizeMeters() / 1609.34f; }
    float totalSizeFeet() const { return totalSizeMeters() * 3.28084f; }
    float totalAreaSqKm() const { return (totalSizeMeters() * totalSizeMeters()) / 1000000.0f; }
    float totalAreaSqMiles() const { return totalSizeMiles() * totalSizeMiles(); }
    int totalChunks() const { return chunkCountX * chunkCountZ; }
};

class EditorUI {
public:
    // Character controller enums (public for external access)
    enum class CameraMode { FirstPerson = 0, ThirdPerson = 1 };
    enum class ControllerType { Character = 0, Vehicle = 1, Flight = 2, Spectator = 3 };
    enum class CollisionHullType { Capsule = 0, Box = 1, Sphere = 2 };

    EditorUI();

    // Render all UI windows
    void render();

    // Setters for display data
    void setFPS(float fps) { m_fps = fps; }
    void setCameraPosition(const glm::vec3& pos) { m_cameraPos = pos; }
    void setBrushPosition(const glm::vec3& pos, bool valid);
    void setCameraSpeed(float speed) { m_cameraSpeed = speed; }
    void setMovementMode(MovementMode mode) { m_movementMode = mode; }
    void setOnGround(bool onGround) { m_onGround = onGround; }

    // Terrain tools toggle
    bool isTerrainToolsEnabled() const { return m_terrainToolsEnabled; }

    // Getters for brush settings
    BrushMode getBrushMode() const { return m_brushMode; }
    void setBrushMode(BrushMode mode) { m_brushMode = mode; }
    float getBrushRadius() const { return m_brushRadius; }
    float getBrushStrength() const { return m_brushStrength; }
    float getBrushFalloff() const { return m_brushFalloff; }
    glm::vec3 getPaintColor() const { return m_paintColor; }
    int getSelectedTexture() const { return m_selectedTexture; }
    BrushShape getBrushShape() const { return m_brushShape; }
    float getBrushShapeAspectRatio() const { return m_brushShapeAspectRatio; }
    float getBrushShapeRotation() const { return m_brushShapeRotation; }

    // Getters for fog settings
    glm::vec3 getFogColor() const { return m_fogColor; }
    float getFogStart() const { return m_fogStart; }
    float getFogEnd() const { return m_fogEnd; }

    // Getters for texture color adjustments (for currently selected texture)
    float getSelectedTexHue() const { return (&m_texHue.x)[m_selectedTexture]; }
    float getSelectedTexSaturation() const { return (&m_texSaturation.x)[m_selectedTexture]; }
    float getSelectedTexBrightness() const { return (&m_texBrightness.x)[m_selectedTexture]; }

    // Callbacks
    void setSpeedChangedCallback(SpeedChangedCallback callback) { m_onSpeedChanged = callback; }
    void setSkyChangedCallback(SkyChangedCallback callback) { m_onSkyChanged = callback; }
    void setClearSelectionCallback(ClearSelectionCallback callback) { m_onClearSelection = callback; }
    void setMoveSelectionCallback(MoveSelectionCallback callback) { m_onMoveSelection = callback; }
    void setTiltSelectionCallback(TiltSelectionCallback callback) { m_onTiltSelection = callback; }
    void setImportModelCallback(ImportModelCallback callback) { m_onImportModel = callback; }
    void setBrowseModelCallback(BrowseModelCallback callback) { m_onBrowseModel = callback; }
    void setSelectObjectCallback(SelectObjectCallback callback) { m_onSelectObject = callback; }
    void setMultiSelectObjectCallback(MultiSelectObjectCallback callback) { m_onMultiSelectObject = callback; }
    void setDeleteObjectCallback(DeleteObjectCallback callback) { m_onDeleteObject = callback; }
    void setGroupObjectsCallback(GroupObjectsCallback callback) { m_onGroupObjects = callback; }
    void setUngroupObjectsCallback(UngroupObjectsCallback callback) { m_onUngroupObjects = callback; }
    void setBulletCollisionChangedCallback(BulletCollisionChangedCallback callback) { m_onBulletCollisionChanged = callback; }
    void setObjectTransformChangedCallback(ObjectTransformChangedCallback callback) { m_onObjectTransformChanged = callback; }
    void setFreezeTransformCallback(FreezeTransformCallback callback) { m_onFreezeTransform = callback; }
    void setApplyPathCallback(ApplyPathCallback callback) { m_onApplyPath = callback; }
    void setClearPathCallback(ClearPathCallback callback) { m_onClearPath = callback; }
    void setUndoPathPointCallback(UndoPathPointCallback callback) { m_onUndoPathPoint = callback; }
    void setCreateTubeCallback(CreateTubeCallback callback) { m_onCreateTube = callback; }
    void setWaterChangedCallback(WaterChangedCallback callback) { m_onWaterChanged = callback; }
    void setFileNewCallback(FileNewCallback callback) { m_onFileNew = callback; }
    void setNewTestLevelCallback(NewTestLevelCallback callback) { m_onNewTestLevel = callback; }
    void setNewSpaceLevelCallback(NewSpaceLevelCallback callback) { m_onNewSpaceLevel = callback; }
    void setFileOpenCallback(FileOpenCallback callback) { m_onFileOpen = callback; }
    void setFileSaveCallback(FileSaveCallback callback) { m_onFileSave = callback; }
    void setFileExitCallback(FileExitCallback callback) { m_onFileExit = callback; }
    void setExportTerrainCallback(ExportTerrainCallback callback) { m_onExportTerrain = callback; }
    void setAddSpawnCallback(AddSpawnCallback callback) { m_onAddSpawn = callback; }
    void setAddCylinderCallback(AddCylinderCallback callback) { m_onAddCylinder = callback; }
    void setAddCubeCallback(AddCubeCallback callback) { m_onAddCube = callback; }
    void setAddDoorCallback(AddDoorCallback callback) { m_onAddDoor = callback; }
    void setRunGameCallback(RunGameCallback callback) { m_onRunGame = callback; }

    // AI Node callbacks
    void setToggleAIPlacementCallback(ToggleAIPlacementCallback callback) { m_onToggleAIPlacement = callback; }
    void setSelectAINodeCallback(SelectAINodeCallback callback) { m_onSelectAINode = callback; }
    void setDeleteAINodeCallback(DeleteAINodeCallback callback) { m_onDeleteAINode = callback; }
    void setAINodePropertyChangedCallback(AINodePropertyChangedCallback callback) { m_onAINodePropertyChanged = callback; }
    void setGenerateAINodesCallback(GenerateAINodesCallback callback) { m_onGenerateAINodes = callback; }
    void setBeginAIConnectionCallback(BeginAIConnectionCallback callback) { m_onBeginAIConnection = callback; }
    void setConnectAINodesCallback(ConnectAINodesCallback callback) { m_onConnectAINodes = callback; }
    void setDisconnectAINodesCallback(DisconnectAINodesCallback callback) { m_onDisconnectAINodes = callback; }
    void setConnectAllGraphNodesCallback(ConnectAllGraphNodesCallback callback) { m_onConnectAllGraphNodes = callback; }
    void setCreateTestEconomyCallback(CreateTestEconomyCallback callback) { m_onCreateTestEconomy = callback; }

    // Path callbacks
    void setCreatePathFromNodesCallback(CreatePathFromNodesCallback callback) { m_onCreatePathFromNodes = callback; }
    void setDeletePathCallback(DeletePathCallback callback) { m_onDeletePath = callback; }
    void setSelectPathCallback(SelectPathCallback callback) { m_onSelectPath = callback; }
    void setPathPropertyChangedCallback(PathPropertyChangedCallback callback) { m_onPathPropertyChanged = callback; }

    // Script callbacks
    void setScriptAddedCallback(ScriptAddedCallback callback) { m_onScriptAdded = callback; }
    void setScriptRemovedCallback(ScriptRemovedCallback callback) { m_onScriptRemoved = callback; }

    // Path management
    void setAIPaths(const std::vector<AIPath*>& paths) { m_aiPaths = paths; }
    int getSelectedPathIndex() const { return m_selectedPathIndex; }
    void setSelectedPathIndex(int index) { m_selectedPathIndex = index; }

    // AI Node multi-selection
    const std::vector<int>& getSelectedAINodeIndices() const { return m_selectedAINodeIndices; }
    void clearAINodeSelection() { m_selectedAINodeIndices.clear(); m_selectedAINodeIndex = -1; }
    bool isAINodeSelected(int index) const {
        return std::find(m_selectedAINodeIndices.begin(), m_selectedAINodeIndices.end(), index) != m_selectedAINodeIndices.end();
    }

    // Connection mode
    bool isConnectionMode() const { return m_aiConnectionMode; }
    void setConnectionMode(bool active) { m_aiConnectionMode = active; }
    int getConnectionSourceIndex() const { return m_aiConnectionSourceIndex; }

    // Window visibility
    bool& showTerrainEditor() { return m_showTerrainEditor; }
    bool& showSkySettings() { return m_showSkySettings; }
    bool& showWaterSettings() { return m_showWaterSettings; }
    bool& showModels() { return m_showModels; }
    bool& showTerrainInfo() { return m_showTerrainInfo; }
    bool& showAINodes() { return m_showAINodes; }
    bool& showHelp() { return m_showHelp; }
    bool& showTechTree() { return m_showTechTree; }
    bool& showGroveEditor() { return m_showGroveEditor; }
    bool& showZones() { return m_showZones; }

    // Behavior script loading
    void setLoadBehaviorScriptCallback(LoadBehaviorScriptCallback cb) { m_onLoadBehaviorScript = std::move(cb); }
    void setListBotScriptsCallback(ListBotScriptsCallback cb) { m_onListBotScripts = std::move(cb); }
    void setLoadBotScriptCallback(LoadBotScriptCallback cb) { m_onLoadBotScript = std::move(cb); }
    void setSaveBotScriptCallback(SaveBotScriptCallback cb) { m_onSaveBotScript = std::move(cb); }

    // Grove script editor
    void setGroveRunCallback(GroveRunCallback cb) { m_onGroveRun = std::move(cb); }
    void setGroveOpenCallback(GroveOpenCallback cb) { m_onGroveOpen = std::move(cb); }
    void setGroveSaveCallback(GroveSaveCallback cb) { m_onGroveSave = std::move(cb); }
    void setGroveSaveAsCallback(GroveSaveAsCallback cb) { m_onGroveSaveAs = std::move(cb); }
    void setGroveFileListCallback(GroveFileListCallback cb) { m_onGroveFileList = std::move(cb); }
    void setGroveOutput(const std::string& output) { m_groveOutput = output; m_groveHasError = false; }
    void setGroveError(const std::string& error, int line) { m_groveOutput = error; m_groveErrorLine = line; m_groveHasError = true; }
    void setGroveLogoDescriptor(void* descriptor) { m_groveLogoDescriptor = descriptor; }
    void setGroveSource(const std::string& source);
    void setGroveCurrentFile(const std::string& path) { m_groveCurrentFile = path; }
    const char* getGroveSource() const { return m_groveSource; }
    const std::string& getGroveCurrentFile() const { return m_groveCurrentFile; }

    // Water settings
    void setWaterLevel(float level) { m_waterLevel = level; }
    float getWaterLevel() const { return m_waterLevel; }
    void setWaveAmplitude(float amp) { m_waveAmplitude = amp; }
    float getWaveAmplitude() const { return m_waveAmplitude; }
    void setWaveFrequency(float freq) { m_waveFrequency = freq; }
    float getWaveFrequency() const { return m_waveFrequency; }
    void setWaterVisible(bool visible) { m_waterVisible = visible; }
    bool getWaterVisible() const { return m_waterVisible; }

    // Path tool state
    void setPathPointCount(size_t count) { m_pathPointCount = count; }
    BrushMode getPathBrushMode() const { return m_pathBrushMode; }

    // Selection state
    void setHasSelection(bool hasSelection) { m_hasSelection = hasSelection; }

    // Model management
    void setSceneObjects(const std::vector<SceneObject*>& objects) { m_sceneObjects = objects; }
    void setSelectedObjectIndex(int index) { m_selectedObjectIndex = index; }
    void setTestLevelMode(bool isTestLevel) {
        m_isTestLevel = isTestLevel;
        if (isTestLevel) {
            // Hide terrain/sky panels in test level mode
            m_showTerrainEditor = false;
            m_showTerrainInfo = false;
            m_showSkySettings = false;
        }
    }
    bool isTestLevel() const { return m_isTestLevel; }
    void setSpaceLevelMode(bool isSpaceLevel) {
        m_isSpaceLevel = isSpaceLevel;
        if (isSpaceLevel) {
            // Hide terrain panels in space level mode, but keep sky settings for stars
            m_showTerrainEditor = false;
            m_showTerrainInfo = false;
        }
    }
    bool isSpaceLevel() const { return m_isSpaceLevel; }
    int getSelectedObjectIndex() const { return m_selectedObjectIndex; }

    // Multi-select support
    const std::set<int>& getSelectedObjectIndices() const { return m_selectedObjectIndices; }
    void setSelectedObjectIndices(const std::set<int>& indices) { m_selectedObjectIndices = indices; }
    bool isObjectSelected(int index) const { return m_selectedObjectIndices.count(index) > 0; }

    // Object groups (organizational only)
    struct ObjectGroup {
        std::string name;
        std::set<int> objectIndices;
        bool expanded = true;
    };
    void setObjectGroups(const std::vector<ObjectGroup>& groups) { m_objectGroups = groups; }
    const std::vector<ObjectGroup>& getObjectGroups() const { return m_objectGroups; }
    void showGroupNamePopup() { m_showGroupNamePopup = true; }

    // Physics backend
    void setPhysicsBackend(PhysicsBackend backend) { m_physicsBackend = backend; }
    PhysicsBackend getPhysicsBackend() const { return m_physicsBackend; }

    // Character controller settings
    CameraMode getCameraMode() const { return m_cameraMode; }
    void setCameraMode(CameraMode mode) { m_cameraMode = mode; }
    ControllerType getControllerType() const { return m_controllerType; }
    void setControllerType(ControllerType type) { m_controllerType = type; }
    CollisionHullType getCollisionHullType() const { return m_collisionHullType; }

    float getThirdPersonDistance() const { return m_thirdPersonDistance; }
    float getThirdPersonHeight() const { return m_thirdPersonHeight; }
    float getThirdPersonLookAtHeight() const { return m_thirdPersonLookAtHeight; }

    float getCharacterSpeed() const { return m_characterSpeed; }
    float getCharacterSprintMultiplier() const { return m_characterSprintMultiplier; }
    float getCharacterJumpVelocity() const { return m_characterJumpVelocity; }
    float getCharacterGravity() const { return m_characterGravity; }
    float getCharacterHeight() const { return m_characterHeight; }
    float getCharacterRadius() const { return m_characterRadius; }

    bool isRagdollEnabled() const { return m_ragdollEnabled; }
    bool isRagdollOnDeath() const { return m_ragdollOnDeath; }

    // Sky parameters
    void setSkyParameters(SkyParameters* params) { m_skyParams = params; }
    SkyParameters* getSkyParameters() { return m_skyParams; }

    // Terrain info
    void setTerrainInfo(const TerrainInfo& info) { m_terrainInfo = info; }

    // AI Node management
    void setAINodes(const std::vector<AINode*>& nodes) { m_aiNodes = nodes; }
    void setSelectedAINodeIndex(int index) { m_selectedAINodeIndex = index; }
    int getSelectedAINodeIndex() const { return m_selectedAINodeIndex; }
    void setAIPlacementMode(bool active) { m_aiPlacementMode = active; }
    bool isAIPlacementMode() const { return m_aiPlacementMode; }
    int getSelectedAINodeType() const { return m_selectedAINodeType; }

    // Zone system
    void setZoneSystem(ZoneSystem* zs) { m_zoneSystem = zs; }
    bool isZoneOverlayEnabled() const { return m_showZoneOverlay; }
    bool isZonePaintMode() const { return m_zonePaintMode; }
    int getZonePaintType() const { return m_zonePaintType; }
    int getZonePaintResource() const { return m_zonePaintResource; }
    float getZonePaintDensity() const { return m_zonePaintDensity; }

    // Config persistence
    void saveConfig(const std::string& filepath);
    void loadConfig(const std::string& filepath);

private:
    void renderMenuBar();
    void renderMainWindow();
    void renderColorSwatches();
    void renderTextureSelector();
    void renderSkySettings();
    void renderModelsWindow();
    void renderPathToolWindow();
    void renderWaterSettings();
    void renderLevelSettings();
    void renderCharacterController();
    void renderTerrainInfo();
    void renderAINodesWindow();
    void renderHelpWindow();
    void renderTechTreeWindow();
    void renderGroveEditor();
    void renderZonesWindow();
    void handleObjectClick(int objectIndex);  // Multi-select logic

    // Display data
    float m_fps = 0.0f;
    glm::vec3 m_cameraPos{0};
    glm::vec3 m_brushPos{0};
    bool m_hasBrushPos = false;
    float m_cameraSpeed = 15.0f;
    MovementMode m_movementMode = MovementMode::Fly;
    bool m_onGround = false;

    // Terrain tools toggle (off by default)
    bool m_terrainToolsEnabled = false;

    // Brush settings
    BrushMode m_brushMode = BrushMode::Raise;
    float m_brushRadius = 15.0f;
    float m_brushStrength = 20.0f;
    float m_brushFalloff = 0.5f;
    BrushShape m_brushShape = BrushShape::Circle;
    float m_brushShapeAspectRatio = 0.3f;  // For ellipse (thin by default)
    float m_brushShapeRotation = 0.0f;     // Rotation in radians

    // Paint colors
    glm::vec3 m_paintColor{0.2f, 0.5f, 0.15f};
    std::vector<glm::vec3> m_colorSwatches;
    int m_selectedSwatch = 0;

    // Texture painting
    int m_selectedTexture = 1;

    // Texture color adjustments (per layer: Grass, Sand, Rock, Snow)
    glm::vec4 m_texHue{0.0f};        // Hue shift in degrees
    glm::vec4 m_texSaturation{1.0f}; // Saturation multiplier
    glm::vec4 m_texBrightness{1.0f}; // Brightness multiplier

    // Fog settings
    glm::vec3 m_fogColor{0.5f, 0.7f, 1.0f};
    float m_fogStart = 1000.0f;
    float m_fogEnd = 2000.0f;

    // Callbacks
    SpeedChangedCallback m_onSpeedChanged;
    SkyChangedCallback m_onSkyChanged;
    ClearSelectionCallback m_onClearSelection;
    MoveSelectionCallback m_onMoveSelection;
    TiltSelectionCallback m_onTiltSelection;
    ImportModelCallback m_onImportModel;
    BrowseModelCallback m_onBrowseModel;
    SelectObjectCallback m_onSelectObject;
    MultiSelectObjectCallback m_onMultiSelectObject;
    DeleteObjectCallback m_onDeleteObject;
    GroupObjectsCallback m_onGroupObjects;
    UngroupObjectsCallback m_onUngroupObjects;
    BulletCollisionChangedCallback m_onBulletCollisionChanged;
    ObjectTransformChangedCallback m_onObjectTransformChanged;
    FreezeTransformCallback m_onFreezeTransform;
    ApplyPathCallback m_onApplyPath;
    ClearPathCallback m_onClearPath;
    UndoPathPointCallback m_onUndoPathPoint;
    CreateTubeCallback m_onCreateTube;
    WaterChangedCallback m_onWaterChanged;
    FileNewCallback m_onFileNew;
    NewTestLevelCallback m_onNewTestLevel;
    NewSpaceLevelCallback m_onNewSpaceLevel;
    FileOpenCallback m_onFileOpen;
    FileSaveCallback m_onFileSave;
    FileExitCallback m_onFileExit;
    ExportTerrainCallback m_onExportTerrain;
    AddSpawnCallback m_onAddSpawn;
    AddCylinderCallback m_onAddCylinder;
    AddCubeCallback m_onAddCube;
    AddDoorCallback m_onAddDoor;
    RunGameCallback m_onRunGame;

    // AI Node callbacks
    ToggleAIPlacementCallback m_onToggleAIPlacement;
    SelectAINodeCallback m_onSelectAINode;
    DeleteAINodeCallback m_onDeleteAINode;
    AINodePropertyChangedCallback m_onAINodePropertyChanged;
    GenerateAINodesCallback m_onGenerateAINodes;
    BeginAIConnectionCallback m_onBeginAIConnection;
    ConnectAINodesCallback m_onConnectAINodes;
    DisconnectAINodesCallback m_onDisconnectAINodes;
    ConnectAllGraphNodesCallback m_onConnectAllGraphNodes;
    CreateTestEconomyCallback m_onCreateTestEconomy;

    // Test level mode
    bool m_isTestLevel = false;
    bool m_isSpaceLevel = false;

    // Physics backend selection
    PhysicsBackend m_physicsBackend = PhysicsBackend::Jolt;

    // Character controller settings
    CameraMode m_cameraMode = CameraMode::FirstPerson;
    ControllerType m_controllerType = ControllerType::Character;
    CollisionHullType m_collisionHullType = CollisionHullType::Capsule;

    // Camera settings
    float m_thirdPersonDistance = 5.0f;
    float m_thirdPersonHeight = 2.0f;
    float m_thirdPersonLookAtHeight = 1.5f;

    // Character movement settings
    float m_characterSpeed = 10.0f;
    float m_characterSprintMultiplier = 2.0f;
    float m_characterJumpVelocity = 8.0f;
    float m_characterGravity = 20.0f;
    float m_characterHeight = 1.8f;
    float m_characterRadius = 0.3f;

    // Ragdoll settings
    bool m_ragdollEnabled = false;
    bool m_ragdollOnDeath = true;

    // Window visibility
    bool m_showCharacterController = false;
    bool m_showLevelSettings = false;
    bool m_showTerrainEditor = true;
    bool m_showSkySettings = true;
    bool m_showWaterSettings = true;
    bool m_showModels = true;
    bool m_showTerrainInfo = true;
    bool m_showAINodes = true;
    bool m_showHelp = false;
    bool m_showTechTree = false;
    bool m_showGroveEditor = false;

    // Grove script editor state
    char m_groveSource[16384] = "";  // 16KB source buffer
    std::string m_groveOutput;
    std::string m_groveCurrentFile;  // Current .grove file path (empty = unsaved)
    int m_groveErrorLine = 0;
    bool m_groveHasError = false;
    bool m_groveModified = false;    // Unsaved changes indicator
    LoadBehaviorScriptCallback m_onLoadBehaviorScript;
    ListBotScriptsCallback m_onListBotScripts;
    LoadBotScriptCallback m_onLoadBotScript;
    SaveBotScriptCallback m_onSaveBotScript;
    GroveRunCallback m_onGroveRun;
    GroveOpenCallback m_onGroveOpen;
    GroveSaveCallback m_onGroveSave;
    GroveSaveAsCallback m_onGroveSaveAs;
    GroveFileListCallback m_onGroveFileList;
    void* m_groveLogoDescriptor = nullptr;

    // Tech tree state
    float m_techTreeZoom = 1.0f;
    glm::vec2 m_techTreePan{0.0f, 0.0f};
    bool m_techTreeDragging = false;
    bool m_techTreeDeathsHeadExpanded = true;  // Collapse state for Death's Head tree

    // Path tool state
    BrushMode m_pathBrushMode = BrushMode::Trench;  // Brush to apply along path
    size_t m_pathPointCount = 0;

    // Tube generation settings
    float m_tubeRadius = 0.15f;
    int m_tubeSegments = 8;
    glm::vec3 m_tubeColor = glm::vec3(0.15f, 0.15f, 0.15f);  // Dark gray wire color

    // Selection state
    bool m_hasSelection = false;

    // Model management
    std::vector<SceneObject*> m_sceneObjects;
    int m_selectedObjectIndex = -1;
    std::set<int> m_selectedObjectIndices;  // Multi-select
    int m_lastClickedObjectIndex = -1;      // For shift-click range selection
    char m_importPath[512] = "";

    // Object groups (organizational)
    std::vector<ObjectGroup> m_objectGroups;
    bool m_showGroupNamePopup = false;
    char m_newGroupName[64] = "New Group";

    // Water settings
    float m_waterLevel = -5.0f;
    float m_waveAmplitude = 0.5f;
    float m_waveFrequency = 0.1f;
    bool m_waterVisible = false;

    // Sky parameters (external, not owned)
    SkyParameters* m_skyParams = nullptr;

    // Terrain info
    TerrainInfo m_terrainInfo;

    // AI Node state
    std::vector<AINode*> m_aiNodes;
    int m_selectedAINodeIndex = -1;
    std::vector<int> m_selectedAINodeIndices;  // Multi-selection support
    int m_lastClickedNodeIndex = -1;  // For shift-click range selection
    bool m_aiPlacementMode = false;
    int m_selectedAINodeType = 0;  // AINodeType as int

    // AI generation settings
    int m_aiGenPattern = 0;
    int m_aiGenCount = 8;
    float m_aiGenRadius = 50.0f;

    // AI connection mode
    bool m_aiConnectionMode = false;
    int m_aiConnectionSourceIndex = -1;

    // Path state
    std::vector<AIPath*> m_aiPaths;
    int m_selectedPathIndex = -1;
    char m_newPathName[64] = "Path_1";

    // Path callbacks
    CreatePathFromNodesCallback m_onCreatePathFromNodes;
    DeletePathCallback m_onDeletePath;
    SelectPathCallback m_onSelectPath;
    PathPropertyChangedCallback m_onPathPropertyChanged;

    // Script callbacks
    ScriptAddedCallback m_onScriptAdded;
    ScriptRemovedCallback m_onScriptRemoved;

    // Zone system
    ZoneSystem* m_zoneSystem = nullptr;
    bool m_showZones = false;
    bool m_showZoneOverlay = false;
    bool m_zonePaintMode = false;
    int m_zonePaintType = 0;      // ZoneType as int
    int m_zonePaintResource = 0;  // ResourceType as int
    float m_zonePaintDensity = 0.8f;
};

} // namespace eden
