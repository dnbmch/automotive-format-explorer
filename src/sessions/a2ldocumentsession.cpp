#include "sessions/a2ldocumentsession.h"
#include "sessions/a2ldetailpresenter.h"
#include "models/memorymapmodel.h"

#pragma push_macro("signals")
#undef signals
#include "a2l/record_layout.pb.h"
#pragma pop_macro("signals")

#include <cstdint>
#include <unordered_map>

A2lDocumentSession::A2lDocumentSession(QString displayName,
                                       QString sourcePath,
                                       a2l::A2lFile document,
                                       QList<DiagnosticMessage> diagnostics)
    : AdapterSessionBase(FormatId::A2L,
                         QStringLiteral("A2L"),
                         std::move(displayName),
                         std::move(sourcePath),
                         std::move(diagnostics)),
      _document(std::move(document)) {
    setDetailPresenter(std::make_unique<A2lDetailPresenter>(_document));
    buildTree();
    buildMemoryMap();
}

A2lDocumentSession::~A2lDocumentSession() = default;

QUrl A2lDocumentSession::centerPanelSource() const {
    if (_memory_map_model && _memory_map_model->totalObjectCount() > 0) {
        return QUrl(QStringLiteral("qrc:/qt/qml/ExplorerApp/qml/components/MemoryView.qml"));
    }
    return {};
}

QAbstractListModel* A2lDocumentSession::centerPanelModel() {
    return _memory_map_model.get();
}

void A2lDocumentSession::moveModelsToThread(QThread* thread) {
    AdapterSessionBase::moveModelsToThread(thread);
    if (_memory_map_model)
        _memory_map_model->moveToThread(thread);
}

namespace {

// Saturating multiply: clamp to UINT64_MAX instead of wrapping, so a malformed
// MATRIX_DIM cannot wrap a size to a small (under-allocating) value.
static uint64_t mulSat(uint64_t a, uint64_t b) {
    if (a != 0 && b > UINT64_MAX / a) return UINT64_MAX;
    return a * b;
}

// DataType -> byte size lookup.
uint64_t dataTypeSize(a2l::DataType dt) {
    switch (dt) {
    case a2l::DATA_TYPE_UBYTE:
    case a2l::DATA_TYPE_SBYTE:
        return 1;
    case a2l::DATA_TYPE_UWORD:
    case a2l::DATA_TYPE_SWORD:
    case a2l::DATA_TYPE_FLOAT16_IEEE:
        return 2;
    case a2l::DATA_TYPE_ULONG:
    case a2l::DATA_TYPE_SLONG:
    case a2l::DATA_TYPE_FLOAT32_IEEE:
        return 4;
    case a2l::DATA_TYPE_A_UINT64:
    case a2l::DATA_TYPE_A_INT64:
    case a2l::DATA_TYPE_FLOAT64_IEEE:
        return 8;
    default:
        return 0;
    }
}

// Color index mapping for object types.
// 0=VALUE, 1=CURVE, 2=MAP, 3=CUBOID+, 4=ASCII, 5=VAL_BLK, 6=MEASUREMENT, 7=AXIS_PTS
int characteristicColorIndex(a2l::CharacteristicType type) {
    switch (type) {
    case a2l::CHARACTERISTIC_TYPE_VALUE:   return 0;
    case a2l::CHARACTERISTIC_TYPE_CURVE:   return 1;
    case a2l::CHARACTERISTIC_TYPE_MAP:     return 2;
    case a2l::CHARACTERISTIC_TYPE_CUBOID:
    case a2l::CHARACTERISTIC_TYPE_CUBE_4:
    case a2l::CHARACTERISTIC_TYPE_CUBE_5:  return 3;
    case a2l::CHARACTERISTIC_TYPE_ASCII:   return 4;
    case a2l::CHARACTERISTIC_TYPE_VAL_BLK: return 5;
    default:                               return 0;
    }
}

struct RecordLayoutInfo {
    a2l::DataType functionValuesType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType axisXType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType axisYType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType identType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType axisCountXType = a2l::DATA_TYPE_UNSPECIFIED;
    a2l::DataType axisCountYType = a2l::DATA_TYPE_UNSPECIFIED;
    bool hasAlternateMode = false;
};

RecordLayoutInfo analyzeRecordLayout(const a2l::RecordLayout& rl) {
    RecordLayoutInfo info;
    for (const auto& comp : rl.components()) {
        if (comp.has_function_values()) {
            info.functionValuesType = comp.function_values().datatype();
            auto mode = comp.function_values().index_mode();
            info.hasAlternateMode =
                (mode == a2l::RECORD_LAYOUT_INDEX_MODE_ALTERNATE_CURVES ||
                 mode == a2l::RECORD_LAYOUT_INDEX_MODE_ALTERNATE_WITH_X ||
                 mode == a2l::RECORD_LAYOUT_INDEX_MODE_ALTERNATE_WITH_Y);
        } else if (comp.has_axis_points()) {
            if (comp.axis_points().axis() == a2l::RECORD_LAYOUT_AXIS_X) {
                info.axisXType = comp.axis_points().datatype();
            } else if (comp.axis_points().axis() == a2l::RECORD_LAYOUT_AXIS_Y) {
                info.axisYType = comp.axis_points().datatype();
            }
        } else if (comp.has_identification()) {
            info.identType = comp.identification().datatype();
        } else if (comp.has_axis_count()) {
            if (comp.axis_count().axis() == a2l::RECORD_LAYOUT_AXIS_X) {
                info.axisCountXType = comp.axis_count().datatype();
            } else if (comp.axis_count().axis() == a2l::RECORD_LAYOUT_AXIS_Y) {
                info.axisCountYType = comp.axis_count().datatype();
            }
        }
    }
    return info;
}

struct SizeResult {
    uint64_t size = 0;
    bool approximate = false;
};

SizeResult computeCharacteristicSize(const a2l::Characteristic& ch,
                                     const RecordLayoutInfo& rlInfo) {
    uint64_t fvSize = dataTypeSize(rlInfo.functionValuesType);
    if (fvSize == 0) {
        return {0, true};
    }

    switch (ch.type()) {
    case a2l::CHARACTERISTIC_TYPE_VALUE:
        return {fvSize, false};

    case a2l::CHARACTERISTIC_TYPE_ASCII: {
        // matrix_dim gives string length, or number field.
        uint64_t len = 0;
        if (ch.matrix_dim_size() > 0) {
            len = ch.matrix_dim(0);
        } else if (ch.has_number()) {
            len = ch.number();
        }
        return {len > 0 ? len : fvSize, len == 0};
    }

    case a2l::CHARACTERISTIC_TYPE_VAL_BLK: {
        uint64_t count = 1;
        for (int i = 0; i < ch.matrix_dim_size(); ++i) {
            count = mulSat(count, ch.matrix_dim(i));
        }
        if (ch.matrix_dim_size() == 0 && ch.has_number()) {
            count = ch.number();
        }
        return {mulSat(count, fvSize), false};
    }

    case a2l::CHARACTERISTIC_TYPE_CURVE: {
        if (rlInfo.hasAlternateMode) {
            // Tier 3: deferred.
            uint32_t axisCount = 0;
            if (ch.axis_descrs_size() > 0) {
                axisCount = ch.axis_descrs(0).max_axis_points();
            }
            uint64_t axSize = dataTypeSize(rlInfo.axisXType);
            uint64_t est = axisCount * (fvSize + axSize);
            return {est > 0 ? est : fvSize, true};
        }
        uint32_t axisCount = 0;
        if (ch.axis_descrs_size() > 0) {
            axisCount = ch.axis_descrs(0).max_axis_points();
        }
        uint64_t axSize = dataTypeSize(rlInfo.axisXType);
        // NO_AXIS_PTS_X header field.
        uint64_t header = dataTypeSize(rlInfo.axisCountXType);
        // Identification header.
        header += dataTypeSize(rlInfo.identType);
        uint64_t total = header + axisCount * axSize + axisCount * fvSize;
        return {total > 0 ? total : fvSize, false};
    }

    case a2l::CHARACTERISTIC_TYPE_MAP: {
        if (rlInfo.hasAlternateMode) {
            uint32_t xCount = 0, yCount = 0;
            if (ch.axis_descrs_size() > 0) xCount = ch.axis_descrs(0).max_axis_points();
            if (ch.axis_descrs_size() > 1) yCount = ch.axis_descrs(1).max_axis_points();
            uint64_t est = xCount * yCount * fvSize;
            return {est > 0 ? est : fvSize, true};
        }
        uint32_t xCount = 0, yCount = 0;
        if (ch.axis_descrs_size() > 0) xCount = ch.axis_descrs(0).max_axis_points();
        if (ch.axis_descrs_size() > 1) yCount = ch.axis_descrs(1).max_axis_points();
        uint64_t axXSize = dataTypeSize(rlInfo.axisXType);
        uint64_t axYSize = dataTypeSize(rlInfo.axisYType);
        uint64_t header = dataTypeSize(rlInfo.axisCountXType)
                        + dataTypeSize(rlInfo.axisCountYType)
                        + dataTypeSize(rlInfo.identType);
        uint64_t total = header + xCount * axXSize + yCount * axYSize
                       + static_cast<uint64_t>(xCount) * yCount * fvSize;
        return {total > 0 ? total : fvSize, false};
    }

    case a2l::CHARACTERISTIC_TYPE_CUBOID:
    case a2l::CHARACTERISTIC_TYPE_CUBE_4:
    case a2l::CHARACTERISTIC_TYPE_CUBE_5: {
        // Tier 3: approximate.
        uint64_t count = 1;
        for (int i = 0; i < ch.axis_descrs_size(); ++i) {
            count *= ch.axis_descrs(i).max_axis_points();
        }
        return {count * fvSize, true};
    }

    default:
        return {fvSize, true};
    }
}

SizeResult computeAxisPtsSize(const a2l::AxisPts& ap,
                              const RecordLayoutInfo& rlInfo) {
    uint64_t axSize = dataTypeSize(rlInfo.axisXType);
    if (axSize == 0) {
        axSize = dataTypeSize(rlInfo.functionValuesType);
    }
    if (axSize == 0) {
        return {0, true};
    }
    uint64_t header = dataTypeSize(rlInfo.axisCountXType)
                    + dataTypeSize(rlInfo.identType);
    uint64_t total = header + ap.max_axis_points() * axSize;
    return {total, false};
}

} // namespace (size calculation helpers)

void A2lDocumentSession::buildMemoryMap() {
    _memory_map_model = std::make_unique<MemoryMapModel>();

    // Build record layout lookup across all modules.
    std::unordered_map<std::string, const a2l::RecordLayout*> rlMap;
    for (int m = 0; m < _document.modules_size(); ++m) {
        const auto& module = _document.modules(m);
        for (int i = 0; i < module.record_layouts_size(); ++i) {
            rlMap[module.record_layouts(i).name()] = &module.record_layouts(i);
        }
    }

    int excludedMeasurements = 0;

    for (int m = 0; m < _document.modules_size(); ++m) {
        const auto& module = _document.modules(m);

        // Add real memory segments.
        if (module.has_mod_par()) {
            for (const auto& seg : module.mod_par().memory_segments()) {
                MemorySegmentInfo info;
                info.name = text(seg.name());
                info.address = seg.address();
                info.size = seg.size();
                info.memoryType = text(
                    a2l::MemoryType_Name(seg.memory_type()));
                info.prgType = text(
                    a2l::MemoryPrgType_Name(seg.prg_type()));
                _memory_map_model->addSegment(std::move(info));
            }
        }

        // Characteristics.
        for (int i = 0; i < module.characteristics_size(); ++i) {
            const auto& ch = module.characteristics(i);
            RecordLayoutInfo rlInfo;
            auto rlIt = rlMap.find(ch.record_layout_ref());
            if (rlIt != rlMap.end()) {
                rlInfo = analyzeRecordLayout(*rlIt->second);
            }

            SizeResult sr = computeCharacteristicSize(ch, rlInfo);

            MemoryObject obj;
            obj.name = text(ch.name());
            obj.longIdentifier = text(ch.long_identifier());
            obj.typeName = text(
                a2l::CharacteristicType_Name(ch.type()));
            obj.address = ch.address();
            obj.size = sr.size;
            obj.sizeApproximate = sr.approximate;
            obj.colorIndex = characteristicColorIndex(ch.type());
            obj.recordLayoutRef = text(ch.record_layout_ref());
            obj.conversion = text(ch.conversion());
            auto keyIt = _tree_node_keys.find({static_cast<int>(A2lEntityKind::Characteristic), m, i});
            if (keyIt != _tree_node_keys.end()) obj.nodeKey = keyIt->second;
            _memory_map_model->addObject(std::move(obj));
        }

        // AxisPts.
        for (int i = 0; i < module.axis_points_size(); ++i) {
            const auto& ap = module.axis_points(i);
            RecordLayoutInfo rlInfo;
            auto rlIt = rlMap.find(ap.record_layout_ref());
            if (rlIt != rlMap.end()) {
                rlInfo = analyzeRecordLayout(*rlIt->second);
            }

            SizeResult sr = computeAxisPtsSize(ap, rlInfo);

            MemoryObject obj;
            obj.name = text(ap.name());
            obj.longIdentifier = text(ap.long_identifier());
            obj.typeName = QStringLiteral("AXIS_PTS");
            obj.address = ap.address();
            obj.size = sr.size;
            obj.sizeApproximate = sr.approximate;
            obj.colorIndex = 7; // AXIS_PTS color
            obj.recordLayoutRef = text(ap.record_layout_ref());
            obj.conversion = text(ap.conversion_ref());
            auto keyIt = _tree_node_keys.find({static_cast<int>(A2lEntityKind::AxisPts), m, i});
            if (keyIt != _tree_node_keys.end()) obj.nodeKey = keyIt->second;
            _memory_map_model->addObject(std::move(obj));
        }

        // Measurements (only those with ecu_address).
        for (int i = 0; i < module.measurements_size(); ++i) {
            const auto& meas = module.measurements(i);
            if (!meas.has_ecu_address()) {
                ++excludedMeasurements;
                continue;
            }

            uint64_t size = dataTypeSize(meas.datatype());
            if (meas.has_array_size() && meas.array_size() > 1) {
                size *= meas.array_size();
            } else if (meas.matrix_dim_size() > 0) {
                uint64_t count = 1;
                for (int d = 0; d < meas.matrix_dim_size(); ++d) {
                    count *= meas.matrix_dim(d);
                }
                size *= count;
            }

            MemoryObject obj;
            obj.name = text(meas.name());
            obj.longIdentifier = text(meas.long_identifier());
            obj.typeName = QStringLiteral("MEASUREMENT");
            obj.address = meas.ecu_address();
            obj.size = size > 0 ? size : 1;
            obj.sizeApproximate = (size == 0);
            obj.colorIndex = 6; // MEASUREMENT color
            auto keyIt = _tree_node_keys.find({static_cast<int>(A2lEntityKind::Measurement), m, i});
            if (keyIt != _tree_node_keys.end()) obj.nodeKey = keyIt->second;
            _memory_map_model->addObject(std::move(obj));
        }
    }

    _memory_map_model->setExcludedMeasurementCount(excludedMeasurements);
    _memory_map_model->finalize();
}

void A2lDocumentSession::buildTree() {
    auto root = std::make_unique<TreeItem>();
    root->title = displayName();
    root->semanticKind = SemanticKind::Root;
    if (_document.has_asap2_version_major() || _document.has_asap2_version_minor()) {
        root->subtitle = QStringLiteral("ASAP2 %1.%2")
            .arg(_document.has_asap2_version_major() ? _document.asap2_version_major() : 0)
            .arg(_document.has_asap2_version_minor() ? _document.asap2_version_minor() : 0);
    }

    TreeItem* modulesSection = appendNode(root.get(),
                                          QStringLiteral("Modules"),
                                          QString::number(_document.modules_size()),
                                          QStringLiteral("modules"),
                                          SemanticKind::Section);

    for (int moduleIndex = 0; moduleIndex < _document.modules_size(); ++moduleIndex) {
        const auto& module = _document.modules(moduleIndex);
        TreeItem* moduleItem = appendNode(modulesSection,
                                          text(module.name()),
                                          text(module.long_identifier()),
                                          QStringLiteral("module"),
                                          SemanticKind::Entity,
                                          NodeBinding{SemanticKind::Entity,
                                                      A2lPath{A2lEntityKind::Module, moduleIndex, -1, -1},
                                                      true});

        if (module.measurements_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Measurements"),
                                           QString::number(module.measurements_size()),
                                           QStringLiteral("measurements"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.measurements_size(); ++i) {
                const auto& item = module.measurements(i);
                TreeItem* node = appendNode(section,
                           text(item.name()),
                           text(a2l::DataType_Name(item.datatype())),
                           QStringLiteral("measurement"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Measurement, moduleIndex, i, -1},
                                       true});
                if (node->nodeKey) {
                    _tree_node_keys[{static_cast<int>(A2lEntityKind::Measurement), moduleIndex, i}] = node->nodeKey;
                }
            }
        }

        if (module.characteristics_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Characteristics"),
                                           QString::number(module.characteristics_size()),
                                           QStringLiteral("characteristics"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.characteristics_size(); ++i) {
                const auto& item = module.characteristics(i);
                TreeItem* node = appendNode(section,
                           text(item.name()),
                           text(a2l::CharacteristicType_Name(item.type())),
                           QStringLiteral("characteristic"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Characteristic, moduleIndex, i, -1},
                                       true});
                if (node->nodeKey) {
                    _tree_node_keys[{static_cast<int>(A2lEntityKind::Characteristic), moduleIndex, i}] = node->nodeKey;
                }
            }
        }

        if (module.axis_points_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Axis Points"),
                                           QString::number(module.axis_points_size()),
                                           QStringLiteral("axispts"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.axis_points_size(); ++i) {
                const auto& item = module.axis_points(i);
                TreeItem* node = appendNode(section,
                           text(item.name()),
                           text(item.record_layout_ref()),
                           QStringLiteral("axispt"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::AxisPts, moduleIndex, i, -1},
                                       true});
                if (node->nodeKey) {
                    _tree_node_keys[{static_cast<int>(A2lEntityKind::AxisPts), moduleIndex, i}] = node->nodeKey;
                }
            }
        }

        if (module.compu_methods_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Compu Methods"),
                                           QString::number(module.compu_methods_size()),
                                           QStringLiteral("compumethods"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.compu_methods_size(); ++i) {
                const auto& item = module.compu_methods(i);
                appendNode(section,
                           text(item.name()),
                           text(a2l::ConversionType_Name(item.conversion_type())),
                           QStringLiteral("compumethod"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::CompuMethod, moduleIndex, i, -1},
                                       true});
            }
        }

        if (module.record_layouts_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Record Layouts"),
                                           QString::number(module.record_layouts_size()),
                                           QStringLiteral("recordlayouts"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.record_layouts_size(); ++i) {
                const auto& item = module.record_layouts(i);
                appendNode(section,
                           text(item.name()),
                           QStringLiteral("%1 components").arg(item.components_size()),
                           QStringLiteral("recordlayout"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::RecordLayout, moduleIndex, i, -1},
                                       true});
            }
        }

        if (module.units_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Units"),
                                           QString::number(module.units_size()),
                                           QStringLiteral("units"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.units_size(); ++i) {
                const auto& item = module.units(i);
                appendNode(section,
                           text(item.name()),
                           text(item.display()),
                           QStringLiteral("unit"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Unit, moduleIndex, i, -1},
                                       true});
            }
        }

        if (module.functions_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Functions"),
                                           QString::number(module.functions_size()),
                                           QStringLiteral("functions"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.functions_size(); ++i) {
                const auto& item = module.functions(i);
                appendNode(section,
                           text(item.name()),
                           QStringLiteral("%1 refs").arg(item.in_measurements_size() +
                                                         item.out_measurements_size() +
                                                         item.def_characteristics_size() +
                                                         item.ref_characteristics_size()),
                           QStringLiteral("function"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Function, moduleIndex, i, -1},
                                       true});
            }
        }

        if (module.groups_size() > 0) {
            TreeItem* section = appendNode(moduleItem,
                                           QStringLiteral("Groups"),
                                           QString::number(module.groups_size()),
                                           QStringLiteral("groups"),
                                           SemanticKind::Section);
            for (int i = 0; i < module.groups_size(); ++i) {
                const auto& item = module.groups(i);
                appendNode(section,
                           text(item.name()),
                           item.has_root() && item.root() ? QStringLiteral("root") : QString(),
                           QStringLiteral("group"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::Group, moduleIndex, i, -1},
                                       true});
            }
        }

        bool hasXcp = false;
        bool hasCcp = false;
        for (const auto& ifData : module.if_datas()) {
            hasXcp = hasXcp || ifData.has_xcp();
            hasCcp = hasCcp || ifData.has_ccp();
        }

        if (hasXcp || hasCcp) {
            TreeItem* protocolSection = appendNode(moduleItem,
                                                   QStringLiteral("Protocols"),
                                                   QString(),
                                                   QStringLiteral("protocols"),
                                                   SemanticKind::Section);
            if (hasXcp) {
                appendNode(protocolSection,
                           QStringLiteral("XCP"),
                           QStringLiteral("module IF_DATA"),
                           QStringLiteral("xcp"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::XcpSummary, moduleIndex, -1, -1},
                                       true});
            }
            if (hasCcp) {
                appendNode(protocolSection,
                           QStringLiteral("CCP"),
                           QStringLiteral("module IF_DATA"),
                           QStringLiteral("ccp"),
                           SemanticKind::Entity,
                           NodeBinding{SemanticKind::Entity,
                                       A2lPath{A2lEntityKind::CcpSummary, moduleIndex, -1, -1},
                                       true});
            }
        }

        if (module.typedef_characteristics_size() > 0 ||
            module.typedef_structures_size() > 0 ||
            module.typedef_axes_size() > 0 ||
            module.instances_size() > 0) {
            TreeItem* typesSection = appendNode(moduleItem,
                                                QStringLiteral("Types"),
                                                QString(),
                                                QStringLiteral("types"),
                                                SemanticKind::Section);

            if (module.typedef_characteristics_size() > 0) {
                TreeItem* section = appendNode(typesSection,
                                               QStringLiteral("Typedef Characteristics"),
                                               QString::number(module.typedef_characteristics_size()),
                                               QStringLiteral("typedefcharacteristics"),
                                               SemanticKind::Section);
                for (int i = 0; i < module.typedef_characteristics_size(); ++i) {
                    const auto& item = module.typedef_characteristics(i);
                    appendNode(section,
                               text(item.name()),
                               text(a2l::CharacteristicType_Name(item.type())),
                               QStringLiteral("typedefcharacteristic"),
                               SemanticKind::Entity,
                               NodeBinding{SemanticKind::Entity,
                                           A2lPath{A2lEntityKind::TypedefItem,
                                                   moduleIndex,
                                                   i,
                                                   kTypedefCharacteristicCategory},
                                           true});
                }
            }

            if (module.typedef_structures_size() > 0) {
                TreeItem* section = appendNode(typesSection,
                                               QStringLiteral("Typedef Structures"),
                                               QString::number(module.typedef_structures_size()),
                                               QStringLiteral("typedefstructures"),
                                               SemanticKind::Section);
                for (int i = 0; i < module.typedef_structures_size(); ++i) {
                    const auto& item = module.typedef_structures(i);
                    appendNode(section,
                               text(item.name()),
                               QStringLiteral("%1 components").arg(item.components_size()),
                               QStringLiteral("typedefstructure"),
                               SemanticKind::Entity,
                               NodeBinding{SemanticKind::Entity,
                                           A2lPath{A2lEntityKind::TypedefItem,
                                                   moduleIndex,
                                                   i,
                                                   kTypedefStructureCategory},
                                           true});
                }
            }

            if (module.typedef_axes_size() > 0) {
                TreeItem* section = appendNode(typesSection,
                                               QStringLiteral("Typedef Axes"),
                                               QString::number(module.typedef_axes_size()),
                                               QStringLiteral("typedefaxes"),
                                               SemanticKind::Section);
                for (int i = 0; i < module.typedef_axes_size(); ++i) {
                    const auto& item = module.typedef_axes(i);
                    appendNode(section,
                               text(item.name()),
                               text(item.input_quantity()),
                               QStringLiteral("typedefaxis"),
                               SemanticKind::Entity,
                               NodeBinding{SemanticKind::Entity,
                                           A2lPath{A2lEntityKind::TypedefItem,
                                                   moduleIndex,
                                                   i,
                                                   kTypedefAxisCategory},
                                           true});
                }
            }

            if (module.instances_size() > 0) {
                TreeItem* section = appendNode(typesSection,
                                               QStringLiteral("Instances"),
                                               QString::number(module.instances_size()),
                                               QStringLiteral("instances"),
                                               SemanticKind::Section);
                for (int i = 0; i < module.instances_size(); ++i) {
                    const auto& item = module.instances(i);
                    appendNode(section,
                               text(item.name()),
                               text(item.type_ref()),
                               QStringLiteral("instance"),
                               SemanticKind::Entity,
                               NodeBinding{SemanticKind::Entity,
                                           A2lPath{A2lEntityKind::Instance, moduleIndex, i, -1},
                                           true});
                }
            }
        }

        if (module.has_variant_coding()) {
            appendNode(moduleItem,
                       QStringLiteral("Variant Coding"),
                       QStringLiteral("%1 criteria").arg(module.variant_coding().var_criteria_size()),
                       QStringLiteral("variantcoding"),
                       SemanticKind::Entity,
                       NodeBinding{SemanticKind::Entity,
                                   A2lPath{A2lEntityKind::VariantCoding, moduleIndex, -1, -1},
                                   true});
        }
    }

    setRootItem(std::move(root));
}
