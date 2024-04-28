#include "UAssetOutlineParser.h"
#include "util.h"

#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <functional>
#include <cctype>
#include <cstdio>

using Byte = std::uint8_t;
using Int16 = std::int16_t;
using Uint16 = std::uint16_t;
using Int32 = std::int32_t;
using Uint32 = std::uint32_t;
using Int64 = std::int64_t;
using Uint64 = std::uint64_t;

class BinaryReader final {
public:
  size_t SeekPos;
  std::vector<Byte> const* Data;
  BinaryReader() : SeekPos(0), Data(nullptr) { }
  BinaryReader(std::vector<Byte> const & data) : SeekPos(0), Data(&data) { }

  void ReadInt64(Int64& output) { readPrimitive(output); }
  void ReadUint64(Uint64& output) { readPrimitive(output); }
  void ReadInt32(Int32& output) { readPrimitive(output); }
  void ReadUint32(Uint32& output) { readPrimitive(output); }
  void ReadInt16(Int16& output) { readPrimitive(output); }
  void ReadUint16(Uint16& output) { readPrimitive(output); }
  void ReadByte(Byte& output) { readPrimitive(output); }
  void ReadInlineUnrealString(std::string& output) {
    output.clear();
    int length; ReadInt32(length);
    if (length < 0) {
      readUcs2StringAndConvertToUtf8(-length, output);
    } else {
      readUtf8String(length, output);
    }
  }

  template<typename T>
  void ReadDeferredUnrealArray(std::vector<T> & output, std::function<T(BinaryReader &)> elementReader) {
    Int32 count; ReadInt32(count);
    Int32 offset; ReadInt32(offset);
    size_t finalSeekPos = SeekPos;
    SeekPos = offset;
    readUnrealArray(output, elementReader, count);
    SeekPos = finalSeekPos;
  }

  template<typename T>
  void ReadInlineUnrealArray(std::vector<T> & output, std::function<T(BinaryReader &)> elementReader) {
    Int32 count; ReadInt32(count);
    readUnrealArray(output, elementReader, count);
  }

  void ReadInt32AndConvertToBool(bool& output) {
    Int32 intForm; ReadInt32(intForm);
    if (intForm != 0 && intForm != 1) {
      int breakHere = 0;
    }
    output = intForm != 0;
  }
private:
  template<typename T>
  void readPrimitive(T& output) {
    if (Data == nullptr) {
      output = {};
      return;
    }
    output = *reinterpret_cast<T const *>(&(*Data)[SeekPos]);
    SeekPos += sizeof(T);
  }

  void readUtf8String(int length, std::string& output) {
    Byte character;
    for (int i = 0; i < length - 1; i++) {
      ReadByte(character);
      output.push_back((char)character);
    }
    ReadByte(character); // Skip null terminator
  }

  void readUcs2StringAndConvertToUtf8(int length, std::string& output) {
    Byte character;
    for (int i = 0; i < (length - 1) * 2; i++) {
      ReadByte(character);
      output.push_back((char)character);
    }
    Uint16 wideCharacter;
    ReadUint16(wideCharacter); // Skip null terminator

    transcodeCharacterStringToUTF8(output, "UCS-2LE");
  }

  template<typename T>
  void readUnrealArray(std::vector<T> & output, std::function<T(BinaryReader &)> elementReader, int count) {
    output.resize(count);
    for (int i = 0; i < count; i++) {
      output[i] = elementReader(*this);
    }
  }
};

struct UnrealGuid final {
public:
  Int32 A;
  Int32 B;
  Int32 C;
  Int32 D;
  static void ReadFrom(BinaryReader & reader, UnrealGuid & output) {
    reader.ReadInt32(output.A);
    reader.ReadInt32(output.B);
    reader.ReadInt32(output.C);
    reader.ReadInt32(output.D);
  }
};

struct UnrealEngineVersion final {
public:
  Uint16 Major;
  Uint16 Minor;
  Uint16 Patch;
  Uint32 ChangelistAndLicenseeBit;
  std::string BranchName;

  static void ReadFrom(BinaryReader & reader, UnrealEngineVersion & output) {
    reader.ReadUint16(output.Major);
    reader.ReadUint16(output.Minor);
    reader.ReadUint16(output.Patch);
    reader.ReadUint32(output.ChangelistAndLicenseeBit);
    reader.ReadInlineUnrealString(output.BranchName);
  }

  static UnrealEngineVersion CreateFromChangelist(Uint32 changelistAndLicenseeBit) {
    UnrealEngineVersion returnValue;
    returnValue.Major = 4;
    returnValue.Minor = 0;
    returnValue.Patch = 0;
    returnValue.ChangelistAndLicenseeBit = changelistAndLicenseeBit;
    returnValue.BranchName = "";
    return returnValue;
  }

  bool IsLicenseeVersion() const {
    return (ChangelistAndLicenseeBit & 0x80'00'00'00) != 0;
  }

  Uint32 GetChangelist() const {
    return ChangelistAndLicenseeBit & 0x7f'ff'ff'ff;
  }
};

struct Archive final {
public:
  static Uint32 constexpr ExpectedMagic = 0x9E'2A'83'C1;

  Uint32 Magic;
  Int32 LegacyVersion;
  Int32 FileVersion;
  Int32 FileVersionUe5;
  Int32 FileLicenseeVersion;

  enum class Ue4Version {
    OldestLoadablePackage = 214,
    BlueprintVarsNotReadOnly = 215,
    StaticMeshStoreNavCollision = 216,
    AtmosphericFogDecayNameChange = 217,
    ScenecompTranslationToLocation = 218,
    MaterialAttributesReordering = 219,
    CollisionProfileSetting = 220,
    BlueprintSkelTemporaryTransient = 221,
    BlueprintSkelSerializedAgain = 222,
    BlueprintSetsReplication = 223,
    WorldLevelInfo = 224,
    AfterCapsuleHalfHeightChange = 225,
    AddedNamespaceAndKeyDataToFtext = 226,
    AttenuationShapes = 227,
    LightcomponentUseIesTextureMultiplierOnNonIesBrightness = 228,
    RemoveInputComponentsFromBlueprints = 229,
    Vark2nodeUseMemberrefstruct = 230,
    RefactorMaterialExpressionScenecolorAndScenedepthInputs = 231,
    SplineMeshOrientation = 232,
    ReverbEffectAssetType = 233,
    MaxTexcoordIncreased = 234,
    SpeedtreeStaticmesh = 235,
    LandscapeComponentLazyReferences = 236,
    SwitchCallNodeToUseMemberReference = 237,
    AddedSkeletonArchiverRemoval = 238,
    AddedSkeletonArchiverRemovalSecondTime = 239,
    BlueprintSkelClassTransientAgain = 240,
    AddCookedToUclass = 241,
    DeprecatedStaticMeshThumbnailPropertiesRemoved = 242,
    CollectionsInShadermapid = 243,
    RefactorMovementComponentHierarchy = 244,
    FixTerrainLayerSwitchOrder = 245,
    AllPropsToConstraintinstance = 246,
    LowQualityDirectionalLightmaps = 247,
    AddedNoiseEmitterComponent = 248,
    AddTextComponentVerticalAlignment = 249,
    AddedFbxAssetImportData = 250,
    RemoveLevelbodysetup = 251,
    RefactorCharacterCrouch = 252,
    SmallerDebugMaterialshaderUniformExpressions = 253,
    ApexCloth = 254,
    SaveCollisionresponsePerChannel = 255,
    AddedLandscapeSplineEditorMesh = 256,
    ChangedMaterialRefactionType = 257,
    RefactorProjectileMovement = 258,
    RemovePhysicalmaterialproperty = 259,
    PurgedFmaterialCompileOutputs = 260,
    AddCookedToLandscape = 261,
    ConsumeInputPerBind = 262,
    SoundClassGraphEditor = 263,
    FixupTerrainLayerNodes = 264,
    RetrofitClampExpressionsSwap = 265,
    RemoveLightMobilityClasses = 266,
    RefactorPhysicsBlending = 267,
    WorldLevelInfoUpdated = 268,
    StaticSkeletalMeshSerializationFix = 269,
    RemoveStaticmeshMobilityClasses = 270,
    RefactorPhysicsTransforms = 271,
    RemoveZeroTriangleSections = 272,
    CharacterMovementDeceleration = 273,
    CameraActorUsingCameraComponent = 274,
    CharacterMovementDeprecatePitchRoll = 275,
    RebuildTextureStreamingDataOnLoad = 276,
    Support_32BITStaticMeshIndices = 277,
    AddedChunkidToAssetdataAndUpackage = 278,
    CharacterDefaultMovementBindings = 279,
    ApexClothLod = 280,
    AtmosphericFogCacheData = 281,
    ArrayPropertyInnerTags = 282,
    KeepSkelMeshIndexData = 283,
    BodysetupCollisionConversion = 284,
    ReflectionCaptureCooking = 285,
    RemoveDynamicVolumeClasses = 286,
    StoreHascookeddataForBodysetup = 287,
    RefractionBiasToRefractionDepthBias = 288,
    RemoveSkeletalphysicsactor = 289,
    PcRotationInputRefactor = 290,
    LandscapePlatformdataCooking = 291,
    CreateexportsClassLinkingForBlueprints = 292,
    RemoveNativeComponentsFromBlueprintScs = 293,
    RemoveSinglenodeinstance = 294,
    CharacterBrakingRefactor = 295,
    VolumeSampleLowQualitySupport = 296,
    SplitTouchAndClickEnables = 297,
    HealthDeathRefactor = 298,
    SoundNodeEnveloperCurveChange = 299,
    PointLightSourceRadius = 300,
    SceneCaptureCameraChange = 301,
    MoveSkeletalmeshShadowcasting = 302,
    ChangeSetarrayBytecode = 303,
    MaterialInstanceBasePropertyOverrides = 304,
    CombinedLightmapTextures = 305,
    BumpedMaterialExportGuids = 306,
    BlueprintInputBindingOverrides = 307,
    FixupBodysetupInvalidConvexTransform = 308,
    FixupStiffnessAndDampingScale = 309,
    ReferenceSkeletonRefactor = 310,
    K2nodeReferenceguids = 311,
    FixupRootboneParent = 312,
    TextRenderComponentsWorldSpaceSizing = 313,
    MaterialInstanceBasePropertyOverridesPhase_2 = 314,
    ClassNotplaceableAdded = 315,
    WorldLevelInfoLodList = 316,
    CharacterMovementVariableRenaming_1 = 317,
    FslatesoundConversion = 318,
    WorldLevelInfoZorder = 319,
    PackageRequiresLocalizationGatherFlagging = 320,
    BpActorVariableDefaultPreventing = 321,
    TestAnimcompChange = 322,
    EditoronlyBlueprints = 323,
    EdgraphpintypeSerialization = 324,
    NoMirrorBrushModelCollision = 325,
    ChangedChunkidToBeAnArrayOfChunkids = 326,
    WorldNamedAfterPackage = 327,
    SkyLightComponent = 328,
    WorldLayerEnableDistanceStreaming = 329,
    RemoveZonesFromModel = 330,
    FixAnimationbaseposeSerialization = 331,
    Support_8BoneInfluencesSkeletalMeshes = 332,
    AddOverrideGravityFlag = 333,
    SupportGpuskinning_8BoneInfluences = 334,
    AnimSupportNonuniformScaleAnimation = 335,
    EngineVersionObject = 336,
    PublicWorlds = 337,
    SkeletonGuidSerialization = 338,
    CharacterMovementWalkableFloorRefactor = 339,
    InverseSquaredLightsDefault = 340,
    DisabledScriptLimitBytecode = 341,
    PrivateRemoteRole = 342,
    FoliageStaticMobility = 343,
    BuildScaleVector = 344,
    FoliageCollision = 345,
    SkyBentNormal = 346,
    LandscapeCollisionDataCooking = 347,
    MorphtargetCpuTangentzdeltaFormatchange = 348,
    SoftConstraintsUseMass = 349,
    ReflectionDataInPackages = 350,
    FoliageMovableMobility = 351,
    UndoBreakMaterialattributesChange = 352,
    AddCustomprofilenameChange = 353,
    FlipMaterialCoords = 354,
    MemberreferenceInPintype = 355,
    VehiclesUnitChange = 356,
    AnimationRemoveNans = 357,
    SkeletonAssetPropertyTypeChange = 358,
    FixBlueprintVariableFlags = 359,
    VehiclesUnitChange2 = 360,
    UclassSerializeInterfacesAfterLinking = 361,
    StaticMeshScreenSizeLods = 362,
    FixMaterialCoords = 363,
    SpeedtreeWindV7 = 364,
    LoadForEditorGame = 365,
    SerializeRichCurveKey = 366,
    MoveLandscapeMicsAndTexturesWithinLevel = 367,
    FtextHistory = 368,
    FixMaterialComments = 369,
    StoreBoneExportNames = 370,
    MeshEmitterInitialOrientationDistribution = 371,
    DisallowFoliageOnBlueprints = 372,
    FixupMotorUnits = 373,
    DeprecatedMovementcomponentModifiedSpeeds = 374,
    RenameCanbecharacterbase = 375,
    GameplayTagContainerTagTypeChange = 376,
    FoliageSettingsType = 377,
    StaticShadowDepthMaps = 378,
    AddTransactionalToDataAssets = 379,
    AddLbWeightblend = 380,
    AddRootcomponentToFoliageactor = 381,
    FixMaterialPropertyOverrideSerialize = 382,
    AddLinearColorSampler = 383,
    AddStringAssetReferencesMap = 384,
    BlueprintUseScsRootcomponentScale = 385,
    LevelStreamingDrawColorTypeChange = 386,
    ClearNotifyTriggers = 387,
    SkeletonAddSmartnames = 388,
    AddedCurrencyCodeToFtext = 389,
    EnumClassSupport = 390,
    FixupWidgetAnimationClass = 391,
    SoundCompressionTypeAdded = 392,
    AutoWelding = 393,
    RenameCrouchmovescharacterdown = 394,
    LightmapMeshBuildSettings = 395,
    RenameSm3ToEs3_1 = 396,
    DeprecateUmgStyleAssets = 397,
    PostDuplicateNodeGuid = 398,
    RenameCameraComponentViewRotation = 399,
    CasePreservingFname = 400,
    RenameCameraComponentControlRotation = 401,
    FixRefractionInputMasking = 402,
    GlobalEmitterSpawnRateScale = 403,
    CleanDestructibleSettings = 404,
    CharacterMovementUpperImpactBehavior = 405,
    BpMathVectorEqualityUsesEpsilon = 406,
    FoliageStaticLightingSupport = 407,
    SlateCompositeFonts = 408,
    RemoveSavegamesummary = 409,
    RemoveSkeletalmeshComponentBodysetupSerialization = 410,
    SlateBulkFontData = 411,
    AddProjectileFrictionBehavior = 412,
    MovementcomponentAxisSettings = 413,
    GraphInteractiveCommentbubbles = 414,
    LandscapeSerializePhysicsMaterials = 415,
    RenameWidgetVisibility = 416,
    AnimationAddTrackcurves = 417,
    MontageBranchingPointRemoval = 418,
    BlueprintEnforceConstInFunctionOverrides = 419,
    AddPivotToWidgetComponent = 420,
    PawnAutoPossessAi = 421,
    FtextHistoryDateTimezone = 422,
    SortActiveBoneIndices = 423,
    PerframeMaterialUniformExpressions = 424,
    MikktspaceIsDefault = 425,
    LandscapeGrassCooking = 426,
    FixSkelVertOrientMeshParticles = 427,
    LandscapeStaticSectionOffset = 428,
    AddModifiersRuntimeGeneration = 429,
    MaterialMaskedBlendmodeTidy = 430,
    MergedAddModifiersRuntimeGenerationTo_4_7Deprecated = 431,
    AfterMergedAddModifiersRuntimeGenerationTo_4_7Deprecated = 432,
    MergedAddModifiersRuntimeGenerationTo_4_7 = 433,
    AfterMergingAddModifiersRuntimeGenerationTo_4_7 = 434,
    SerializeLandscapeGrassData = 435,
    OptionallyClearGpuEmittersOnInit = 436,
    SerializeLandscapeGrassDataMaterialGuid = 437,
    BlueprintGeneratedClassComponentTemplatesPublic = 438,
    ActorComponentCreationMethod = 439,
    K2nodeEventMemberReference = 440,
    StructGuidInPropertyTag = 441,
    RemoveUnusedUpolysFromUmodel = 442,
    RebuildHierarchicalInstanceTrees = 443,
    PackageSummaryHasCompatibleEngineVersion = 444,
    TrackUcsModifiedProperties = 445,
    LandscapeSplineCrossLevelMeshes = 446,
    DeprecateUserWidgetDesignSize = 447,
    AddEditorViews = 448,
    FoliageWithAssetOrClass = 449,
    BodyinstanceBinarySerialization = 450,
    SerializeBlueprintEventgraphFastcallsInUfunction = 451,
    InterpcurveSupportsLooping = 452,
    MaterialInstanceBasePropertyOverridesDitheredLodTransition = 453,
    SerializeLandscapeEs2Textures = 454,
    ConstraintInstanceMotorFlags = 455,
    SerializePintypeConst = 456,
    LibraryCategoriesAsFtext = 457,
    SkipDuplicateExportsOnSavePackage = 458,
    SerializeTextInPackages = 459,
    AddBlendModeToWidgetComponent = 460,
    NewLightmassPrimitiveSetting = 461,
    ReplaceSpringNozProperty = 462,
    TightlyPackedEnums = 463,
    AssetImportDataAsJson = 464,
    TextureLegacyGamma = 465,
    AddedNativeSerializationForImmutableStructures = 466,
    DeprecateUmgStyleOverrides = 467,
    StaticShadowmapPenumbraSize = 468,
    NiagaraDataObjectDevUiFix = 469,
    FixedDefaultOrientationOfWidgetComponent = 470,
    RemovedMaterialUsedWithUiFlag = 471,
    CharacterMovementAddBrakingFriction = 472,
    BspUndoFix = 473,
    DynamicParameterDefaultValue = 474,
    StaticMeshExtendedBounds = 475,
    AddedNonLinearTransitionBlends = 476,
    AoMaterialMask = 477,
    NavigationAgentSelector = 478,
    MeshParticleCollisionsConsiderParticleSize = 479,
    BuildMeshAdjBufferFlagExposed = 480,
    MaxAngularVelocityDefault = 481,
    ApexClothTessellation = 482,
    DecalSize = 483,
    KeepOnlyPackageNamesInStringAssetReferencesMap = 484,
    CookedAssetsInEditorSupport = 485,
    DialogueWaveNamespaceAndContextChanges = 486,
    MakeRotRenameAndReorder = 487,
    K2nodeVarReferenceguids = 488,
    SoundConcurrencyPackage = 489,
    UserwidgetDefaultFocusableFalse = 490,
    BlueprintCustomEventConstInput = 491,
    UseLowPassFilterFreq = 492,
    NoAnimBpClassInGameplayCode = 493,
    ScsStoresAllnodesArray = 494,
    FbxImportDataRangeEncapsulation = 495,
    CameraComponentAttachToRoot = 496,
    InstancedStereoUniformUpdate = 497,
    StreamableTextureMinMaxDistance = 498,
    InjectBlueprintStructPinConversionNodes = 499,
    InnerArrayTagInfo = 500,
    FixSlotNameDuplication = 501,
    StreamableTextureAabb = 502,
    PropertyGuidInPropertyTag = 503,
    NameHashesSerialized = 504,
    InstancedStereoUniformRefactor = 505,
    CompressedShaderResources = 506,
    PreloadDependenciesInCookedExports = 507,
    TemplateindexInCookedExports = 508,
    PropertyTagSetMapSupport = 509,
    AddedSearchableNames = 510,
    SixtyFourBitExportmapSerialsizes = 511,
    SkylightMobileIrradianceMap = 512,
    AddedSweepWhileWalkingFlag = 513,
    AddedSoftObjectPath = 514,
    PointlightSourceOrientation = 515,
    AddedPackageSummaryLocalizationId = 516,
    FixWideStringCrc = 517,
    AddedPackageOwner = 518,
    SkinweightProfileDataLayoutChanges = 519,
    NonOuterPackageImport = 520,
    AssetregistryDependencyflags = 521,
    CorrectLicenseeFlag = 522
  };

  enum class Ue5Version {
    InitialVersion = 1000,
    NamesReferencedFromExportData = 1001,
    PayloadToc = 1002,
    OptionalResources = 1003,
    LargeWorldCoordinates = 1004,
    RemoveObjectExportPackageGuid = 1005,
    TrackObjectExportIsInherited = 1006,
    FSoftObjectPathRemoveAssetPathFNames = 1007,
    AddSoftObjectPathList = 1008,
    DataResources = 1009,
  };

  static void ReadFrom(BinaryReader& reader, Archive & output) {
    reader.ReadUint32(output.Magic); // TODO: check Magic == ExpectedMagic

    reader.ReadInt32(output.LegacyVersion); // TODO: check -8 <= LegacyVersion <= -5

    Int32 unusedLegacyUe3Version;
    reader.ReadInt32(unusedLegacyUe3Version);

    reader.ReadInt32(output.FileVersion);
    if (output.LegacyVersion <= -8) {
      reader.ReadInt32(output.FileVersionUe5);
    } else {
      output.FileVersionUe5 = 0;
    }

    reader.ReadInt32(output.FileLicenseeVersion);
  }

  static int constexpr CustomVersionSize = 20;
  static int constexpr GuidCustomVersionPrefixSize = 20;

  enum class CustomVersionSerializationFormat {
    Optimized, Guids
  };
  CustomVersionSerializationFormat GetCustomVersionSerializationFormat() const {
    return LegacyVersion < -5
      ? CustomVersionSerializationFormat::Optimized
      : CustomVersionSerializationFormat::Guids;
  }
};

struct UnrealName final {
public:
  Int32 ComparisonIndex;
  Uint32 Number;
  //Int32 DisplayIndex;
  static void ReadFrom(BinaryReader & reader, UnrealName & output) {
    reader.ReadInt32(output.ComparisonIndex);
    reader.ReadUint32(output.Number);
    //reader.ReadInt32(output.DisplayIndex);
  }
};

struct SoftObjectPath final {
public:
  // Maybe an FGuid?
  Int32 Unknown0;
  Int32 Unknown1;
  Int32 Unknown2;
  Int32 Unknown3;

  std::string Path;

  static void ReadFrom(BinaryReader & reader, SoftObjectPath & output) {
    reader.ReadInt32(output.Unknown0);
    reader.ReadInt32(output.Unknown1);
    reader.ReadInt32(output.Unknown2);
    reader.ReadInt32(output.Unknown3);
    reader.ReadInlineUnrealString(output.Path);
  }
};

struct ObjectExport final {
public:
  Int32 ClassIndex;
  Int32 SuperIndex;
  Int32 TemplateIndex;
  Int32 OuterIndex;
  UnrealName ObjectName;
  Uint32 ObjectFlags;
  Int64 SerialSize;
  Int64 SerialOffset;
  bool ForcedExport;
  bool NotForClient;
  bool NotForServer;
  bool IsInheritedInstance;
  Uint32 PackageFlags;
  bool NotAlwaysLoadedForEditorGame;
  bool IsAsset;
  bool GeneratePublicHash;
  Int32 FirstExportDependency;
  Int32 SerializationBeforeSerializationDependencies;
  Int32 CreateBeforeSerializationDependencies;
  Int32 SerializationBeforeCreateDependencies;
  Int32 CreateBeforeCreateDependencies;

  static void ReadFrom(BinaryReader & reader, Archive const & archive, struct AssetHeader const & assetHeader, ObjectExport & output);
};

struct ObjectImport final {
public:
  UnrealName ClassPackage;
  UnrealName ClassName;
  Int32 OuterIndex;
  UnrealName ObjectName;
  UnrealName PackageName;
  bool ImportOptional;
  static void ReadFrom(BinaryReader & reader, Archive const & archive, struct AssetHeader const & assetHeader, ObjectImport & output);
};

struct AssetHeader final {
public:
  enum class PackageFlags : Uint32 {
    None = 0x00000000,
    NewlyCreated = 0x00000001,
    ClientOptional = 0x00000002,
    ServerSideOnly = 0x00000004,
    CompiledIn = 0x00000010,
    ForDiffing = 0x00000020,
    EditorOnly = 0x00000040,
    Developer = 0x00000080,
    UncookedOnly = 0x00000100,
    Cooked = 0x00000200,
    ContainsNoAsset = 0x00000400,
    Unused1 = 0x00000800,
    Unused2 = 0x00001000,
    UnversionedProperties = 0x00002000,
    ContainsMapData = 0x00004000,
    Unused3 = 0x00008000,
    Compiling = 0x00010000,
    ContainsMap = 0x00020000,
    RequiresLocalizationGather = 0x00040000,
    Unused4 = 0x00080000,
    PlayInEditor = 0x00100000,
    ContainsScript = 0x00200000,
    DisallowExport = 0x00400000,
    Unused5 = 0x00800000,
    Unused6 = 0x01000000,
    Unused7 = 0x02000000,
    Unused8 = 0x04000000,
    Unused9 = 0x08000000,
    DynamicImports = 0x10000000,
    RuntimeGenerated = 0x20000000,
    ReloadingForCooker = 0x40000000,
    FilterEditorOnly = 0x80000000,
  };

  Int32 TotalHeaderSize;
  std::string FolderName;

  Uint32 PackageFlags;

  std::vector<std::string> Names;

  //Int32 SoftObjectPathsCount;
  //Int32 SoftObjectPathsOffset;
  std::vector<SoftObjectPath> SoftObjectPaths;

  std::string LocalizationId;
  Int32 GatherableTextDataCount;
  Int32 GatherableTextDataOffset;

  //Int32 ExportCount;
  //Int32 ExportOffset;
  std::vector<ObjectExport> Exports;

  //Int32 ImportCount;
  //Int32 ImportOffset;
  std::vector<ObjectImport> Imports;

  Int32 DependsOffset;
  Int32 SoftPackageReferencesCount;
  Int32 SoftPackageReferencesOffset;
  Int32 SearchableNamesOffset;
  Int32 ThumbnailTableOffset;
  UnrealEngineVersion SavedByEngineVersion;
  UnrealEngineVersion CompatibleWithEngineVersion;
  Uint32 CompressionFlags;
  Uint32 PackageSource;
  Int32 AssetRegistryDataOffset;
  Int64 BulkDataStartOffset;
  Int32 WorldTileInfoDataOffset;
  std::vector<Int32> ChunkIds;
  Int32 PreloadDependencyCount;
  Int32 PreloadDependencyOffset;
  Int32 NamesReferencedFromExportDataCount;
  Int64 PayloadTocOffset;
  Int32 DataResourceOffset;

  constexpr static int GuidSize = 16;
  constexpr static int GenerationInfoSize = 8;
  constexpr static int CompressedChunkSize = 16;

  static void ReadFrom(BinaryReader & reader, Archive const & archive, AssetHeader & output) {
    Archive::CustomVersionSerializationFormat customSerializationFormat = archive.GetCustomVersionSerializationFormat();

    int customVersionCount; reader.ReadInt32(customVersionCount);
    for (int i = 0; i < customVersionCount; i++) {
      switch (customSerializationFormat) {
        case Archive::CustomVersionSerializationFormat::Optimized:
          reader.SeekPos += Archive::CustomVersionSize;
          break;
        case Archive::CustomVersionSerializationFormat::Guids:
          reader.SeekPos += Archive::GuidCustomVersionPrefixSize;
          std::string throwawayString; reader.ReadInlineUnrealString(throwawayString);
          break;
      }
    }

    reader.ReadInt32(output.TotalHeaderSize);

    reader.ReadInlineUnrealString(output.FolderName);

    reader.ReadUint32(output.PackageFlags);
    bool packageHasEditorOnlyData = (output.PackageFlags & (Uint32)PackageFlags::FilterEditorOnly) == 0;

    reader.ReadDeferredUnrealArray<std::string>(
      output.Names,
      [&archive](BinaryReader & reader) -> std::string {
        std::string newString; reader.ReadInlineUnrealString(newString);
        if (archive.FileVersion >= (int)Archive::Ue4Version::NameHashesSerialized) {
          Uint32 throwawayHash; reader.ReadUint32(throwawayHash);
        }
        return newString;
      });

    reader.ReadDeferredUnrealArray<SoftObjectPath>(
      output.SoftObjectPaths,
      [](BinaryReader & reader) -> SoftObjectPath {
        SoftObjectPath result; SoftObjectPath::ReadFrom(reader, result);
        return result;
      });

    if (archive.FileVersion >= (int)Archive::Ue4Version::AddedPackageSummaryLocalizationId
        && packageHasEditorOnlyData) {
      reader.ReadInlineUnrealString(output.LocalizationId);
    }

    conditionallyReadDeferredArrayCountAndOffset(
      archive.FileVersion >= (int)Archive::Ue4Version::SerializeTextInPackages,
      reader,
      output.GatherableTextDataCount,
      output.GatherableTextDataOffset,
      0,
      0);

    reader.ReadDeferredUnrealArray<ObjectExport>(
      output.Exports,
      [&archive, &output](BinaryReader & reader) -> ObjectExport {
        ObjectExport result; ObjectExport::ReadFrom(reader, archive, output, result);
        return result;
      });

    reader.ReadDeferredUnrealArray<ObjectImport>(
      output.Imports,
      [&archive, &output](BinaryReader & reader) -> ObjectImport {
        ObjectImport result; ObjectImport::ReadFrom(reader, archive, output, result);
        return result;
      });

    reader.ReadInt32(output.DependsOffset);

    conditionallyReadDeferredArrayCountAndOffset(
      archive.FileVersion >= (int)Archive::Ue4Version::AddStringAssetReferencesMap,
      reader,
      output.SoftPackageReferencesCount,
      output.SoftPackageReferencesOffset,
      0,
      0);

    if (archive.FileVersion >= (int)Archive::Ue4Version::AddedSearchableNames) {
      reader.ReadInt32(output.SearchableNamesOffset);
    } else {
      output.SearchableNamesOffset = 0;
    }

    reader.ReadInt32(output.ThumbnailTableOffset);

    reader.SeekPos += GuidSize; // Guid
    if (archive.FileVersion >= (int)Archive::Ue4Version::AddedPackageOwner
        && packageHasEditorOnlyData) {
      reader.SeekPos += GuidSize; // PersistentGuid
      if (archive.FileVersion < (int)Archive::Ue4Version::NonOuterPackageImport) {
        reader.SeekPos += GuidSize; // OwnerPersistentGuid
      }
    }

    int numGenerations; reader.ReadInt32(numGenerations);
    for (int i = 0; i < numGenerations; i++) {
      reader.SeekPos += GenerationInfoSize;
    }

    if (archive.FileVersion >= (int)Archive::Ue4Version::EngineVersionObject) {
      UnrealEngineVersion::ReadFrom(reader, output.SavedByEngineVersion);
    } else {
      Uint32 engineChangelist; reader.ReadUint32(engineChangelist);
      output.SavedByEngineVersion = UnrealEngineVersion::CreateFromChangelist(engineChangelist);
    }

    if (archive.FileVersion >= (int)Archive::Ue4Version::PackageSummaryHasCompatibleEngineVersion) {
      UnrealEngineVersion::ReadFrom(reader, output.CompatibleWithEngineVersion);
    } else {
      output.CompatibleWithEngineVersion = output.SavedByEngineVersion;
    }

    reader.ReadUint32(output.CompressionFlags);

    Int32 compressedChunkCount; reader.ReadInt32(compressedChunkCount);
    for (int i = 0; i < compressedChunkCount; i++) {
      reader.SeekPos += CompressedChunkSize;
    }

    reader.ReadUint32(output.PackageSource);

    std::vector<std::string> throwawayAdditionalPackagesToCook;
    reader.ReadInlineUnrealArray<std::string>(
      throwawayAdditionalPackagesToCook,
      [](BinaryReader& reader) -> std::string {
        std::string returnValue; reader.ReadInlineUnrealString(returnValue);
        return returnValue;
      });

    if (archive.LegacyVersion > -7) {
      Int32 textureAllocations; reader.ReadInt32(textureAllocations);
    }

    reader.ReadInt32(output.AssetRegistryDataOffset);
    reader.ReadInt64(output.BulkDataStartOffset);

    if (archive.FileVersion >= (int)Archive::Ue4Version::WorldLevelInfo) {
      reader.ReadInt32(output.WorldTileInfoDataOffset);
    } else {
      output.WorldTileInfoDataOffset = 0;
    }

    if (archive.FileVersion >= (int)Archive::Ue4Version::AddedChunkidToAssetdataAndUpackage) {
      if (archive.FileVersion >= (int)Archive::Ue4Version::ChangedChunkidToBeAnArrayOfChunkids) {
        reader.ReadInlineUnrealArray<Int32>(output.ChunkIds, [](BinaryReader& reader) -> Int32 {
          Int32 returnValue; reader.ReadInt32(returnValue);
          return returnValue;
        });
      } else {
        Int32 singleChunkId; reader.ReadInt32(singleChunkId);
        output.ChunkIds.push_back(singleChunkId);
      }
    }

    conditionallyReadDeferredArrayCountAndOffset(
      archive.FileVersion >= (int)Archive::Ue4Version::PreloadDependenciesInCookedExports,
      reader,
      output.PreloadDependencyCount,
      output.PreloadDependencyOffset,
      -1,
      0);

    if (archive.FileVersionUe5 >= (int)Archive::Ue5Version::NamesReferencedFromExportData) {
      reader.ReadInt32(output.NamesReferencedFromExportDataCount);
    } else {
      output.NamesReferencedFromExportDataCount = output.Names.size();
    }

    if (archive.FileVersionUe5 >= (int)Archive::Ue5Version::PayloadToc) {
      reader.ReadInt64(output.PayloadTocOffset);
    } else {
      output.PayloadTocOffset = -1;
    }

    output.DataResourceOffset = 0;
    if (archive.FileVersionUe5 >= (int)Archive::Ue5Version::DataResources) {
      int offset; reader.ReadInt32(offset);
      if (offset > 0) { output.DataResourceOffset = offset; }
    }
  }

  std::string NameToString(const UnrealName& name) const {
    return Names[name.ComparisonIndex];
  }


  std::string ClassIndexToObjectNameString(const Int32 index) const {
    if (index > 0) {
      return NameToString(Exports[index - 1].ObjectName);
    } else {
      return NameToString(Imports[(-index) - 1].ObjectName);
    }
  }

  std::string ClassIndexToClassNameString(const Int32 index) const {
    if (index > 0) {
      return ClassIndexToObjectNameString(Exports[index - 1].ClassIndex);
    } else {
      return NameToString(Imports[(-index) - 1].ClassName);
    }
  }
private:
  static void conditionallyReadDeferredArrayCountAndOffset(bool condition, BinaryReader & reader, Int32 & count, Int32 & offset, Int32 defaultCount, Int32 defaultOffset) {
    if (condition) {
      reader.ReadInt32(count);
      reader.ReadInt32(offset);
    } else {
      count = defaultCount;
      offset = defaultOffset;
    }
  }
};

void ObjectExport::ReadFrom(BinaryReader & reader, Archive const & archive, AssetHeader const & assetHeader, ObjectExport & output) {
  output = ObjectExport();
  reader.ReadInt32(output.ClassIndex);
  reader.ReadInt32(output.SuperIndex);
  if (archive.FileVersion >= (int)Archive::Ue4Version::TemplateindexInCookedExports) {
    reader.ReadInt32(output.TemplateIndex);
  }
  UnrealName::ReadFrom(reader, output.ObjectName);
  Int32 Unknown0; reader.ReadInt32(Unknown0);
  reader.ReadUint32(output.ObjectFlags);
  if (archive.FileVersion < (int)Archive::Ue4Version::SixtyFourBitExportmapSerialsizes) {
    Int32 serialSize32; reader.ReadInt32(serialSize32); output.SerialSize = serialSize32;
    Int32 serialOffset32; reader.ReadInt32(serialOffset32); output.SerialOffset = serialOffset32;
  } else {
    reader.ReadInt64(output.SerialSize);
    reader.ReadInt64(output.SerialOffset);
  }
  reader.ReadInt32AndConvertToBool(output.ForcedExport);
  reader.ReadInt32AndConvertToBool(output.NotForClient);
  reader.ReadInt32AndConvertToBool(output.NotForServer);
  if (archive.FileVersionUe5 < (int)Archive::Ue5Version::RemoveObjectExportPackageGuid) {
    UnrealGuid throwawayGuid; UnrealGuid::ReadFrom(reader, throwawayGuid);
  }
  if (archive.FileVersionUe5 >= (int)Archive::Ue5Version::TrackObjectExportIsInherited) {
    reader.ReadInt32AndConvertToBool(output.IsInheritedInstance);
  }
  reader.ReadUint32(output.PackageFlags);
  if (archive.FileVersion >= (int)Archive::Ue4Version::LoadForEditorGame) {
    reader.ReadInt32AndConvertToBool(output.NotAlwaysLoadedForEditorGame);
  }
  if (archive.FileVersion >= (int)Archive::Ue4Version::CookedAssetsInEditorSupport) {
    reader.ReadInt32AndConvertToBool(output.IsAsset);
  }
  if (archive.FileVersionUe5 >= (int)Archive::Ue5Version::OptionalResources) {
    reader.ReadInt32AndConvertToBool(output.GeneratePublicHash);
  }
  if (archive.FileVersion >= (int)Archive::Ue4Version::PreloadDependenciesInCookedExports) {
    reader.ReadInt32(output.FirstExportDependency);
    reader.ReadInt32(output.SerializationBeforeCreateDependencies);
    reader.ReadInt32(output.CreateBeforeSerializationDependencies);
    reader.ReadInt32(output.SerializationBeforeCreateDependencies);
    reader.ReadInt32(output.CreateBeforeCreateDependencies);
  }
}

void ObjectImport::ReadFrom(BinaryReader & reader, Archive const & archive, AssetHeader const & assetHeader, ObjectImport & output) {
  bool packageHasEditorOnlyData = (assetHeader.PackageFlags & (Uint32)AssetHeader::PackageFlags::FilterEditorOnly) == 0;

  output = ObjectImport();
  UnrealName::ReadFrom(reader, output.ClassPackage);
  UnrealName::ReadFrom(reader, output.ClassName);
  reader.ReadInt32(output.OuterIndex);
  UnrealName::ReadFrom(reader, output.ObjectName);
  if (packageHasEditorOnlyData && archive.FileVersion >= (int)Archive::Ue4Version::NonOuterPackageImport) {
    UnrealName::ReadFrom(reader, output.PackageName);
  }
  if (archive.FileVersionUe5 >= (int)Archive::Ue5Version::OptionalResources) {
    reader.ReadInt32AndConvertToBool(output.ImportOptional);
  }
}

struct UnrealPropertyTag final {
public:
  struct StructExtraData final {
  public:
    UnrealName StructName;
    std::optional<UnrealGuid> StructGuid;
  };
  struct BoolExtraData final {
  public:
    bool BoolVal;
  };
  struct EnumExtraData final {
  public:
    UnrealName EnumName;
  };
  struct ArrayExtraData final {
  public:
    std::optional<UnrealName> InnerType;
  };
  struct OptionalExtraData final {
  public:
    UnrealName InnerType;
  };
  struct SetExtraData final {
  public:
    UnrealName InnerType;
  };
  struct MapExtraData final {
  public:
    UnrealName KeyType;
    UnrealName ValueType;
  };

  UnrealName Name;
  UnrealName Type;
  Int32 Size;
  Int32 ArrayIndex;

  std::optional<std::variant<
    StructExtraData,
    BoolExtraData,
    EnumExtraData,
    ArrayExtraData,
    OptionalExtraData,
    SetExtraData,
    MapExtraData>> ExtraData;

  std::optional<UnrealGuid> PropertyGuid;

  static void ReadFrom(BinaryReader & reader, Archive const & archive, AssetHeader const & assetHeader, UnrealPropertyTag & output) {
    output = UnrealPropertyTag();

    UnrealName::ReadFrom(reader, output.Name);
    std::string nameStr = assetHeader.Names[output.Name.ComparisonIndex];
    if (nameStr == "None") { return; }

    UnrealName::ReadFrom(reader, output.Type);

    reader.ReadInt32(output.Size);
    reader.ReadInt32(output.ArrayIndex);

    std::string typeStr = assetHeader.Names[output.Type.ComparisonIndex];

    if (output.Type.Number == 0) {
      if (typeStr == "StructProperty") {
        StructExtraData extraData = {};
        UnrealName::ReadFrom(reader, extraData.StructName);
        if (archive.FileVersion >= (int)Archive::Ue4Version::StructGuidInPropertyTag) {
          UnrealGuid structGuid; UnrealGuid::ReadFrom(reader, structGuid);
          extraData.StructGuid = structGuid;
        }
        output.ExtraData = extraData;
      } else if (typeStr == "BoolProperty") {
        BoolExtraData extraData = {};
        Byte boolVal; reader.ReadByte(boolVal);
        extraData.BoolVal = boolVal != 0;
        output.ExtraData = extraData;
      } else if (typeStr == "EnumName") {
        EnumExtraData extraData = {};
        UnrealName::ReadFrom(reader, extraData.EnumName);
        output.ExtraData = extraData;
      } else if (typeStr == "ArrayProperty") {
        ArrayExtraData extraData = {};
        extraData.InnerType = std::nullopt;
        if (archive.FileVersion >= (int)Archive::Ue4Version::ArrayPropertyInnerTags) {
          UnrealName innerType; UnrealName::ReadFrom(reader, innerType);
          extraData.InnerType = innerType;
        }
        output.ExtraData = extraData;
      } else if (typeStr == "OptionalProperty") {
        OptionalExtraData extraData = {};
        UnrealName::ReadFrom(reader, extraData.InnerType);
        output.ExtraData = extraData;
      } else if (archive.FileVersion >= (int)Archive::Ue4Version::PropertyTagSetMapSupport) {
        if (typeStr == "SetProperty") {
          SetExtraData extraData = {};
          UnrealName::ReadFrom(reader, extraData.InnerType);
          output.ExtraData = extraData;
        } else if (typeStr == "MapProperty") {
          MapExtraData extraData = {};
          UnrealName::ReadFrom(reader, extraData.KeyType);
          UnrealName::ReadFrom(reader, extraData.ValueType);
          output.ExtraData = extraData;
        }
      }
      int breakHere = 0;
    }

    output.PropertyGuid = std::nullopt;
    if (archive.FileVersion >= (int)Archive::Ue4Version::PropertyGuidInPropertyTag) {
      Byte hasPropertyGuid; reader.ReadByte(hasPropertyGuid);
      if (hasPropertyGuid != 0) {
        UnrealGuid propertyGuid; UnrealGuid::ReadFrom(reader, propertyGuid);
        output.PropertyGuid = propertyGuid;
      }
    }
  }
};

void UAssetOutlineParser::parseBinaryInput(const QCString& fileName, const std::vector<Byte>& fileBuf, const std::shared_ptr<Entry>& root)
{
  BinaryReader binaryReader(fileBuf);
  Archive archive; Archive::ReadFrom(binaryReader, archive);
  AssetHeader assetHeader; AssetHeader::ReadFrom(binaryReader, archive, assetHeader);

  for (int i = 1; i <= assetHeader.Exports.size(); i++) {
    std::string exportClass = assetHeader.ClassIndexToClassNameString(i);

    if (exportClass == "Blueprint") {
      size_t prevPos = binaryReader.SeekPos;
      binaryReader.SeekPos = assetHeader.Exports[i - 1].SerialOffset;
      while (true) {
        UnrealPropertyTag propertyTag; UnrealPropertyTag::ReadFrom(binaryReader, archive, assetHeader, propertyTag);
        std::string nameStr = assetHeader.Names[propertyTag.Name.ComparisonIndex];
        if (nameStr == "None") { break; }
        binaryReader.SeekPos += propertyTag.Size; // Let's pretend we read the value here
      }
      binaryReader.SeekPos = prevPos;
    }
  }

  for (int i = 1; i <= assetHeader.Imports.size(); i++) {
    std::string importClass = assetHeader.ClassIndexToClassNameString(-i);
    int breakHere1 = 0;
  }

  int breakHere = 0;
}

bool UAssetOutlineParser::needsPreprocessing(const QCString& extension) const
{
    return false;
}

void UAssetOutlineParser::parsePrototype(const QCString& text) { }
