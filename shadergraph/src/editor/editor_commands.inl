namespace {

bool applyOne(DocumentState& state, const Json& command, CommandResult& result, bool& structural,
              bool& layoutChanged, Json& extra, std::set<NodeId>& affected) {
    structural = false;
    layoutChanged = false;
    std::string op;
    if (!getString(command, "op", op, result)) {
        return false;
    }

    if (op == "addNode") {
        std::string type;
        if (!getString(command, "type", type, result)) {
            return false;
        }
        const NodeDef* definition = state.registry.find(type);
        if (definition == nullptr) {
            return fail(result, kErrorUnknownNode, "unknown node type: " + type);
        }
        if (state.graph.kind == GraphKind::Graph && !definition->allowInMainGraph) {
            return fail(result, kErrorTargetUnsupported,
                        "node type is not available in the main graph");
        }
        if (state.graph.kind == GraphKind::SubGraph && !definition->allowInSubGraph) {
            return fail(result, kErrorTargetUnsupported,
                        "node type is not available in a subgraph");
        }
        if (!nodeTargetAllowed(state.graph, *definition)) {
            return fail(result, kErrorTargetUnsupported,
                        "node type is not available for the current target");
        }
        NodeId id = state.graph.nextNodeId();
        if (command.contains("id") && !readNodeId(command, "id", id, result)) {
            return false;
        }
        if (state.graph.findNode(id) != nullptr) {
            return fail(result, kErrorDuplicateId, "node id already exists: " + std::to_string(id));
        }
        Vec2 position;
        if (!getVec2(command, "position", position, result, false)) {
            return false;
        }
        NodeInstance node = state.registry.instantiate(*definition, id, position);
        if (command.contains("settings") && !applyNodeSettings(node, command["settings"], result)) {
            return false;
        }
        if (definition->updateSlots && !definition->updateSlots(state.graph, node)) {
            return fail(result, kErrorInvalidValue, "node settings could not be applied");
        }
        std::string error;
        if (!state.graph.addNode(std::move(node), &error)) {
            return fail(result, kErrorDuplicateId, error);
        }
        addMissingLayoutEntries(state);
        structural = true;
        affected.insert(id);
        extra["id"] = id;
        return true;
    }

    if (op == "removeNodes") {
        if (!command.contains("ids") || !command["ids"].is_array()) {
            return fail(result, kErrorInvalidJson, "ids must be an array");
        }
        std::vector<NodeId> ids;
        for (const Json& value : command["ids"]) {
            if (!value.is_number_integer() && !value.is_number_unsigned()) {
                return fail(result, kErrorInvalidJson, "ids must contain integers");
            }
            ids.push_back(value.get<NodeId>());
        }
        for (NodeId id : ids) {
            if (state.graph.findNode(id) == nullptr) {
                return fail(result, kEditorNotFound, "node not found: " + std::to_string(id));
            }
        }
        for (NodeId id : ids) {
            std::string error;
            state.graph.removeNode(id, &error);
            eraseLayoutNode(state, id);
            affected.insert(id);
        }
        structural = true;
        return true;
    }

    if (op == "duplicateNodes") {
        if (!command.contains("ids") || !command["ids"].is_array()) {
            return fail(result, kErrorInvalidJson, "ids must be an array");
        }
        Vec2 offset;
        if (!getVec2(command, "offset", offset, result, false)) {
            return false;
        }
        std::vector<NodeId> ids;
        for (const Json& value : command["ids"]) {
            if (!value.is_number_integer() && !value.is_number_unsigned()) {
                return fail(result, kErrorInvalidJson, "ids must contain integers");
            }
            ids.push_back(value.get<NodeId>());
        }
        std::map<NodeId, NodeId> idMap;
        NodeId nextId = state.graph.nextNodeId();
        for (NodeId id : ids) {
            if (state.graph.findNode(id) == nullptr) {
                return fail(result, kEditorNotFound, "node not found: " + std::to_string(id));
            }
            idMap[id] = nextId++;
        }
        for (NodeId id : ids) {
            const NodeInstance* source = state.graph.findNode(id);
            NodeInstance copy = *source;
            copy.id = idMap[id];
            copy.position.x += offset.x;
            copy.position.y += offset.y;
            for (SlotInstance& slot : copy.slots) {
                slot.connected = false;
                if (slot.state.mode == SlotMode::Connected) {
                    slot.state.mode = slot.state.value ? SlotMode::Literal : SlotMode::Default;
                }
            }
            std::string error;
            if (!state.graph.addNode(std::move(copy), &error)) {
                return fail(result, kErrorInvalidValue, error);
            }
            affected.insert(idMap[id]);
        }
        const std::vector<Edge> edges = state.graph.edges;
        for (const Edge& edge : edges) {
            const auto from = idMap.find(edge.fromNode);
            const auto to = idMap.find(edge.toNode);
            if (from == idMap.end() || to == idMap.end()) {
                continue;
            }
            if (state.graph.incomingEdge(to->second, edge.toSlot) != nullptr) {
                continue;
            }
            Edge copied;
            copied.fromNode = from->second;
            copied.fromSlot = edge.fromSlot;
            copied.toNode = to->second;
            copied.toSlot = edge.toSlot;
            std::string error;
            if (!state.graph.addEdge(copied, &error)) {
                return fail(result, kErrorInvalidEdge, error);
            }
        }
        addMissingLayoutEntries(state);
        structural = true;
        extra["idMap"] = Json::object();
        for (const auto& [oldId, newId] : idMap) {
            extra["idMap"][std::to_string(oldId)] = newId;
        }
        return true;
    }

    if (op == "moveNodes") {
        if (!command.contains("nodes") || !command["nodes"].is_array()) {
            return fail(result, kErrorInvalidJson, "nodes must be an array");
        }
        Vec2 delta;
        if (!getVec2(command, "delta", delta, result, false)) {
            return false;
        }
        for (const Json& value : command["nodes"]) {
            if (!value.is_number_integer() && !value.is_number_unsigned()) {
                return fail(result, kErrorInvalidJson, "nodes must contain integers");
            }
            const NodeId id = value.get<NodeId>();
            NodeInstance* node = state.graph.findNode(id);
            if (node == nullptr) {
                return fail(result, kEditorNotFound, "node not found: " + std::to_string(id));
            }
            node->position.x += delta.x;
            node->position.y += delta.y;
            auto layoutNode = std::find_if(state.layout.nodes.begin(), state.layout.nodes.end(),
                                           [id](const NodeLayout& item) { return item.id == id; });
            if (layoutNode == state.layout.nodes.end()) {
                state.layout.nodes.push_back({id, node->position, false});
            } else {
                layoutNode->position = node->position;
            }
            affected.insert(id);
        }
        layoutChanged = true;
        return true;
    }

    if (op == "setNodeSetting") {
        NodeId id = kInvalidNode;
        if (!readNodeId(command, "node", id, result)) {
            return false;
        }
        NodeInstance* node = findNode(state, id, result);
        if (node == nullptr) {
            return false;
        }
        std::string name;
        if (!getString(command, "name", name, result)) {
            return false;
        }
        if (!command.contains("value")) {
            return fail(result, kErrorInvalidJson, "missing field: value");
        }
        if (name == "precision") {
            if (!command["value"].is_string()) {
                return fail(result, kErrorInvalidJson, "precision must be a string");
            }
            const auto parsed = precisionFromString(command["value"].get<std::string>());
            if (!parsed) {
                return fail(result, kErrorInvalidValue, "unknown node precision");
            }
            node->precision = *parsed;
        } else {
            const Value* current = node->setting(name);
            std::string error;
            auto parsed = detail::parseValue(
                command["value"],
                current == nullptr ? std::nullopt : std::optional<ValueType>{current->type}, &error);
            if (!parsed) {
                return fail(result, kErrorTypeMismatch, "invalid node setting: " + error);
            }
            node->settings.insert_or_assign(name, std::move(*parsed));
        }
        if (const NodeDef* definition = state.registry.find(node->type);
            definition != nullptr && definition->updateSlots &&
            !definition->updateSlots(state.graph, *node)) {
            return fail(result, kErrorInvalidValue, "node settings could not be applied");
        }
        structural = true;
        affected.insert(id);
        return true;
    }

    if (op == "setSlotValue") {
        NodeId nodeId = kInvalidNode;
        SlotId slotId = kInvalidSlot;
        if (!readNodeId(command, "node", nodeId, result) ||
            !readSlotId(command, "slot", slotId, result)) {
            return false;
        }
        NodeInstance* node = findNode(state, nodeId, result);
        if (node == nullptr) {
            return false;
        }
        SlotInstance* slot = findSlot(*node, slotId, result);
        if (slot == nullptr) {
            return false;
        }
        if (!slot->isInput()) {
            return fail(result, kErrorInvalidValue, "slot is not an input");
        }
        std::string modeText;
        if (!getString(command, "mode", modeText, result)) {
            return false;
        }
        const auto mode = slotModeFromString(modeText);
        if (!mode) {
            return fail(result, kErrorInvalidValue, "unknown slot mode: " + modeText);
        }
        if (command.contains("concreteType")) {
            if (!command["concreteType"].is_string()) {
                return fail(result, kErrorInvalidJson, "concreteType must be a string");
            }
            const auto concrete = concreteTypeFromString(command["concreteType"].get<std::string>());
            if (!concrete) {
                return fail(result, kErrorInvalidValue, "unknown slot concrete type");
            }
            std::string typeError;
            if (!state.graph.setSlotConcreteType(nodeId, slotId, *concrete, &typeError)) {
                return fail(result, kErrorTypeMismatch, typeError);
            }
        }
        if (*mode == SlotMode::Connected) {
            if (!hasIncoming(state.graph, nodeId, slotId)) {
                return fail(result, kErrorInvalidEdge, "input slot is not connected");
            }
            slot->state.mode = SlotMode::Connected;
            slot->state.value.reset();
            slot->connected = true;
        } else {
            if (const Edge* incoming = state.graph.incomingEdge(nodeId, slotId);
                incoming != nullptr) {
                const Edge edge = *incoming;
                state.graph.removeEdge(edge);
            }
            slot->connected = false;
            if (*mode == SlotMode::Literal) {
                if (!command.contains("value")) {
                    return fail(result, kErrorInvalidJson, "literal mode requires a value");
                }
                Value value;
                if (!parseValue(command["value"], slot->spec.type, value, result)) {
                    return false;
                }
                slot->state.mode = SlotMode::Literal;
                slot->state.value = std::move(value);
            } else {
                slot->state.mode = SlotMode::Default;
                slot->state.value.reset();
            }
        }
        structural = true;
        affected.insert(nodeId);
        return true;
    }

    if (op == "setSlotConcreteType") {
        NodeId nodeId = kInvalidNode;
        SlotId slotId = kInvalidSlot;
        if (!readNodeId(command, "node", nodeId, result) ||
            !readSlotId(command, "slot", slotId, result)) {
            return false;
        }
        NodeInstance* node = findNode(state, nodeId, result);
        if (node == nullptr) {
            return false;
        }
        if (findSlot(*node, slotId, result) == nullptr) {
            return false;
        }
        std::string typeText;
        if (!getString(command, "concreteType", typeText, result)) {
            return false;
        }
        const auto type = concreteTypeFromString(typeText);
        if (!type) {
            return fail(result, kErrorInvalidValue, "unknown concrete type: " + typeText);
        }
        std::string error;
        if (!state.graph.setSlotConcreteType(nodeId, slotId, *type, &error)) {
            return fail(result, kErrorTypeMismatch, error);
        }
        structural = true;
        affected.insert(nodeId);
        return true;
    }

    if (op == "connect") {
        Endpoint from;
        Endpoint to;
        if (!readEndpoint(command, "from", from, result) ||
            !readEndpoint(command, "to", to, result)) {
            return false;
        }
        NodeInstance* fromNode = findNode(state, from.node, result);
        NodeInstance* toNode = findNode(state, to.node, result);
        if (fromNode == nullptr || toNode == nullptr) {
            return false;
        }
        SlotInstance* fromSlot = findSlot(*fromNode, from.slot, result);
        SlotInstance* toSlot = findSlot(*toNode, to.slot, result);
        if (fromSlot == nullptr || toSlot == nullptr) {
            return false;
        }
        if (!fromSlot->isOutput() || !toSlot->isInput()) {
            return fail(result, kErrorInvalidEdge, "edge must connect an output to an input");
        }
        if (!fromSlot->spec.allowConnection || !toSlot->spec.allowConnection) {
            return fail(result, kErrorInvalidEdge, "slot does not allow connections");
        }
        if (toSlot->spec.type != ValueType::PropertyConnectionState &&
            !isCompatible(toSlot->spec.type, fromSlot->concreteType)) {
            return fail(result, kErrorTypeMismatch, "slot types are not compatible");
        }
        if (from.node == to.node || reaches(state.graph, to.node, from.node)) {
            return fail(result, kErrorCycle, "connection would create a cycle");
        }
        Edge edge{from.node, from.slot, to.node, to.slot};
        std::string error;
        if (!state.graph.addEdge(edge, &error)) {
            return fail(result, kErrorInvalidEdge, error);
        }
        structural = true;
        affected.insert(from.node);
        affected.insert(to.node);
        extra["edge"] = {{"from", {{"node", from.node}, {"slot", from.slot}}},
                         {"to", {{"node", to.node}, {"slot", to.slot}}}};
        return true;
    }

    if (op == "disconnect") {
        if (!command.contains("from") && !command.contains("to")) {
            return fail(result, kErrorInvalidJson, "disconnect requires from or to");
        }
        Endpoint from;
        Endpoint to;
        const bool hasFrom = command.contains("from");
        const bool hasTo = command.contains("to");
        if (hasFrom && !readEndpoint(command, "from", from, result)) {
            return false;
        }
        if (hasTo && !readEndpoint(command, "to", to, result)) {
            return false;
        }
        int removed = 0;
        const std::vector<Edge> edges = state.graph.edges;
        for (const Edge& edge : edges) {
            if (hasFrom && (edge.fromNode != from.node || edge.fromSlot != from.slot)) {
                continue;
            }
            if (hasTo && (edge.toNode != to.node || edge.toSlot != to.slot)) {
                continue;
            }
            state.graph.removeEdge(edge);
            affected.insert(edge.fromNode);
            affected.insert(edge.toNode);
            ++removed;
        }
        if (removed == 0) {
            return fail(result, kEditorNotFound, "no edge matched the disconnect request");
        }
        structural = true;
        return true;
    }

    if (op == "addProperty") {
        std::string name;
        std::string refName;
        if (!getString(command, "name", name, result)) {
            return false;
        }
        if (!normalizeRefName(name, true, refName, result)) {
            return false;
        }
        if (command.contains("refName")) {
            if (!getString(command, "refName", refName, result) ||
                !normalizeRefName(refName, true, refName, result)) {
                return false;
            }
        }
        std::string typeText = "float";
        if (!getString(command, "type", typeText, result, false)) {
            return false;
        }
        const auto type = valueTypeFromString(typeText);
        if (!type) {
            return fail(result, kErrorInvalidValue, "unknown property type: " + typeText);
        }
        Property property;
        property.name = std::move(name);
        property.refName = std::move(refName);
        property.type = *type;
        std::string precisionText = "inherit";
        if (!getString(command, "precision", precisionText, result, false)) {
            return false;
        }
        if (const auto precision = precisionFromString(precisionText)) {
            property.precision = *precision;
        } else {
            return fail(result, kErrorInvalidValue, "unknown property precision");
        }
        if (command.contains("default")) {
            Value value;
            if (!parseValue(command["default"], property.type, value, result)) {
                return false;
            }
            property.defaultValue = std::move(value);
        } else {
            property.defaultValue = defaultValueFor(property.type).value_or(Value{});
        }
        if (!getString(command, "category", property.category, result, false) ||
            !getString(command, "floatMode", property.floatMode, result, false) ||
            !getString(command, "defaultType", property.defaultType, result, false)) {
            return false;
        }
        if (command.contains("exposed")) {
            if (!command["exposed"].is_boolean()) {
                return fail(result, kErrorInvalidJson, "exposed must be a boolean");
            }
            property.exposed = command["exposed"].get<bool>();
        }
        if (command.contains("enumNames")) {
            if (!command["enumNames"].is_array()) {
                return fail(result, kErrorInvalidJson, "enumNames must be an array");
            }
            for (const Json& name : command["enumNames"]) {
                if (!name.is_string()) {
                    return fail(result, kErrorInvalidJson, "enumNames must contain strings");
                }
                property.enumNames.push_back(name.get<std::string>());
            }
        }
        if (command.contains("enumValues")) {
            if (!command["enumValues"].is_array()) {
                return fail(result, kErrorInvalidJson, "enumValues must be an array");
            }
            for (const Json& value : command["enumValues"]) {
                if (!value.is_number_integer()) {
                    return fail(result, kErrorInvalidJson, "enumValues must contain integers");
                }
                property.enumValues.push_back(value.get<int>());
            }
        }
        if (property.enumValues.size() > property.enumNames.size()) {
            return fail(result, kErrorInvalidValue, "enumValues exceeds enumNames");
        }
        while (property.enumValues.size() < property.enumNames.size()) {
            property.enumValues.push_back(static_cast<int>(property.enumValues.size()));
        }
        if (command.contains("attributes")) {
            if (!command["attributes"].is_array()) {
                return fail(result, kErrorInvalidJson, "attributes must be an array");
            }
            for (const Json& attribute : command["attributes"]) {
                if (!attribute.is_string()) {
                    return fail(result, kErrorInvalidJson, "attributes must contain strings");
                }
                property.attributes.push_back(attribute.get<std::string>());
            }
        }
        if (command.contains("sampler") && command["sampler"].is_object()) {
            if (!getString(command["sampler"], "filter", property.samplerFilter, result, false) ||
                !getString(command["sampler"], "wrap", property.samplerWrap, result, false)) {
                return false;
            }
        }
        if (command.contains("useTilingAndOffset")) {
            if (!command["useTilingAndOffset"].is_boolean()) {
                return fail(result, kErrorInvalidJson,
                            "useTilingAndOffset must be a boolean");
            }
            property.useTilingAndOffset = command["useTilingAndOffset"].get<bool>();
        }
        if (!getVec2(command, "tiling", property.tiling, result, false) ||
            !getVec2(command, "offset", property.offset, result, false)) {
            return false;
        }
        if (property.useTilingAndOffset && property.type != ValueType::Texture2D) {
            return fail(result, kErrorInvalidValue,
                        "useTilingAndOffset requires a Texture2D property");
        }
        if (!getInteger(command, "vtLayers", property.vtLayers, result, false)) {
            return false;
        }
        if (property.type == ValueType::VirtualTexture &&
            (property.vtLayers < 1 || property.vtLayers > 4)) {
            return fail(result, kErrorInvalidValue,
                        "vtLayers must be between 1 and 4");
        }
        if (command.contains("vtLayerRefNames")) {
            if (!command["vtLayerRefNames"].is_array()) {
                return fail(result, kErrorInvalidJson, "vtLayerRefNames must be an array");
            }
            for (const Json& layerName : command["vtLayerRefNames"]) {
                if (!layerName.is_string() || layerName.get<std::string>().empty()) {
                    return fail(result, kErrorInvalidJson,
                                "vtLayerRefNames must contain non-empty strings");
                }
                property.vtLayerRefNames.push_back(layerName.get<std::string>());
            }
        }
        if (command.contains("vtLayerTextureTypes")) {
            if (!command["vtLayerTextureTypes"].is_array()) {
                return fail(result, kErrorInvalidJson,
                            "vtLayerTextureTypes must be an array");
            }
            for (const Json& layerType : command["vtLayerTextureTypes"]) {
                if (!layerType.is_string()) {
                    return fail(result, kErrorInvalidJson,
                                "vtLayerTextureTypes must contain strings");
                }
                const auto normalized =
                    virtualTextureLayerTextureTypeFromString(layerType.get<std::string>());
                if (!normalized) {
                    return fail(result, kErrorInvalidValue,
                                "unknown virtual texture layer texture type: " +
                                    layerType.get<std::string>());
                }
                property.vtLayerTextureTypes.push_back(*normalized);
            }
        }
        if (property.type != ValueType::VirtualTexture &&
            (!property.vtLayerRefNames.empty() || !property.vtLayerTextureTypes.empty())) {
            return fail(result, kErrorInvalidValue,
                        "virtual texture layer metadata requires a VirtualTexture property");
        }
        if ((!property.vtLayerRefNames.empty() &&
             property.vtLayerRefNames.size() != static_cast<std::size_t>(property.vtLayers)) ||
            (!property.vtLayerTextureTypes.empty() &&
             property.vtLayerTextureTypes.size() != static_cast<std::size_t>(property.vtLayers))) {
            return fail(result, kErrorInvalidValue,
                        "virtual texture layer arrays must match vtLayers");
        }
        std::string error;
        if (!state.graph.addProperty(std::move(property), &error)) {
            return fail(result, kErrorNameConflict, error);
        }
        structural = true;
        return true;
    }

    if (op == "removeProperty") {
        std::string refName;
        if (!getString(command, "refName", refName, result)) {
            return false;
        }
        if (state.graph.findProperty(refName) == nullptr) {
            return fail(result, kEditorNotFound, "property not found: " + refName);
        }
        state.graph.removeProperty(refName);
        std::vector<NodeId> nodes;
        for (const NodeInstance& node : state.graph.nodes) {
            if (node.type == "Property" && propertyRef(node) == refName) {
                nodes.push_back(node.id);
            }
        }
        for (NodeId id : nodes) {
            state.graph.removeNode(id);
            eraseLayoutNode(state, id);
            affected.insert(id);
        }
        structural = true;
        return true;
    }

    if (op == "renameProperty") {
        std::string refName;
        std::string newName;
        if (!getString(command, "refName", refName, result) ||
            !getString(command, "newName", newName, result)) {
            return false;
        }
        Property* property = state.graph.findProperty(refName);
        if (property == nullptr) {
            return fail(result, kEditorNotFound, "property not found: " + refName);
        }
        if (newName.empty()) {
            return fail(result, kErrorInvalidValue, "newName must not be empty");
        }
        property->name = std::move(newName);
        structural = true;
        return true;
    }

    if (op == "setProperty") {
        std::string refName;
        std::string field;
        if (!getString(command, "refName", refName, result) ||
            !getString(command, "field", field, result)) {
            return false;
        }
        Property* property = state.graph.findProperty(refName);
        if (property == nullptr) {
            return fail(result, kEditorNotFound, "property not found: " + refName);
        }
        if (!command.contains("value")) {
            return fail(result, kErrorInvalidJson, "missing field: value");
        }
        const Json& value = command["value"];
        if (field == "default" || field == "defaultValue") {
            Value parsed;
            if (!parseValue(value, property->type, parsed, result)) {
                return false;
            }
            property->defaultValue = std::move(parsed);
        } else if (field == "precision") {
            if (!value.is_string()) {
                return fail(result, kErrorInvalidJson, "precision must be a string");
            }
            const auto parsed = precisionFromString(value.get<std::string>());
            if (!parsed) {
                return fail(result, kErrorInvalidValue, "unknown property precision");
            }
            property->precision = *parsed;
        } else if (field == "category" || field == "name" || field == "refName" ||
                   field == "floatMode" || field == "defaultType") {
            if (!value.is_string()) {
                return fail(result, kErrorInvalidJson, "field must be a string: " + field);
            }
            const std::string text = value.get<std::string>();
            if (field == "category") {
                property->category = text;
            } else if (field == "name") {
                property->name = text;
            } else if (field == "floatMode") {
                property->floatMode = text;
            } else if (field == "defaultType") {
                property->defaultType = text;
            } else {
                if (state.graph.findProperty(text) != nullptr || !isIdentifier(text)) {
                    return fail(result, kErrorNameConflict,
                                "property refName is invalid or already exists");
                }
                const std::string oldRef = property->refName;
                property->refName = text;
                for (NodeInstance& node : state.graph.nodes) {
                    if (node.type == "Property" && propertyRef(node) == oldRef) {
                        node.setSetting("property", Value::makeString(ValueType::String, text));
                    }
                }
            }
        } else if (field == "exposed" || field == "useTilingAndOffset") {
            if (!value.is_boolean()) {
                return fail(result, kErrorInvalidJson, "field must be a boolean: " + field);
            }
            if (field == "exposed") {
                property->exposed = value.get<bool>();
            } else {
                property->useTilingAndOffset = value.get<bool>();
                if (property->useTilingAndOffset && property->type != ValueType::Texture2D) {
                    return fail(result, kErrorInvalidValue,
                                "useTilingAndOffset requires a Texture2D property");
                }
            }
        } else if (field == "tiling" || field == "offset") {
            if (!value.is_array() || value.size() != 2 || !value[0].is_number() ||
                !value[1].is_number()) {
                return fail(result, kErrorInvalidJson,
                            "field must be a two-number array: " + field);
            }
            Vec2 parsed{value[0].get<float>(), value[1].get<float>()};
            if (field == "tiling") {
                property->tiling = parsed;
            } else {
                property->offset = parsed;
            }
        } else if (field == "vtLayers") {
            if (!value.is_number_integer() && !value.is_number_unsigned()) {
                return fail(result, kErrorInvalidJson,
                            "field must be an integer: " + field);
            }
            const int layers = value.get<int>();
            if (layers < 1 || layers > 4) {
                return fail(result, kErrorInvalidValue,
                            "vtLayers must be between 1 and 4");
            }
            if (property->vtLayers != layers &&
                (!property->vtLayerRefNames.empty() ||
                 !property->vtLayerTextureTypes.empty())) {
                property->vtLayerRefNames.clear();
                property->vtLayerTextureTypes.clear();
            }
            property->vtLayers = layers;
        } else if (field == "vtLayerRefNames" || field == "vtLayerTextureTypes") {
            if (property->type != ValueType::VirtualTexture) {
                return fail(result, kErrorInvalidValue,
                            "virtual texture layer metadata requires a VirtualTexture property");
            }
            if (!value.is_array()) {
                return fail(result, kErrorInvalidJson,
                            "field must be an array: " + field);
            }
            std::vector<std::string> parsed;
            parsed.reserve(value.size());
            for (const Json& entry : value) {
                if (!entry.is_string()) {
                    return fail(result, kErrorInvalidJson,
                                "field entries must be strings: " + field);
                }
                if (field == "vtLayerTextureTypes") {
                    const auto normalized =
                        virtualTextureLayerTextureTypeFromString(entry.get<std::string>());
                    if (!normalized) {
                        return fail(result, kErrorInvalidValue,
                                    "unknown virtual texture layer texture type: " +
                                        entry.get<std::string>());
                    }
                    parsed.push_back(*normalized);
                } else {
                    if (entry.get<std::string>().empty()) {
                        return fail(result, kErrorInvalidJson,
                                    "vtLayerRefNames must contain non-empty strings");
                    }
                    parsed.push_back(entry.get<std::string>());
                }
            }
            if (parsed.size() != static_cast<std::size_t>(property->vtLayers)) {
                return fail(result, kErrorInvalidValue,
                            "virtual texture layer arrays must match vtLayers");
            }
            if (field == "vtLayerRefNames") {
                property->vtLayerRefNames = std::move(parsed);
            } else {
                property->vtLayerTextureTypes = std::move(parsed);
            }
        } else if (field == "attributes") {
            if (!value.is_array()) {
                return fail(result, kErrorInvalidJson, "attributes must be an array");
            }
            property->attributes.clear();
            for (const Json& attribute : value) {
                if (!attribute.is_string()) {
                    return fail(result, kErrorInvalidJson, "attributes must contain strings");
                }
                property->attributes.push_back(attribute.get<std::string>());
            }
        } else {
            return fail(result, kErrorInvalidValue, "unknown property field: " + field);
        }
        structural = true;
        return true;
    }

    if (op == "addKeyword") {
        std::string name;
        std::string refName;
        if (!getString(command, "name", name, result) ||
            !normalizeRefName(name, true, refName, result)) {
            return false;
        }
        if (command.contains("refName")) {
            if (!getString(command, "refName", refName, result) ||
                !normalizeRefName(refName, true, refName, result)) {
                return false;
            }
        }
        Keyword keyword;
        keyword.name = std::move(name);
        keyword.refName = std::move(refName);
        std::string typeText = "boolean";
        if (!getString(command, "type", typeText, result, false)) {
            return false;
        }
        const auto type = keywordTypeFromString(typeText);
        if (!type) {
            return fail(result, kErrorInvalidValue, "unknown keyword type: " + typeText);
        }
        keyword.type = *type;
        std::string definitionText = "shaderfeature";
        std::string scopeText = "local";
        if (!getString(command, "definition", definitionText, result, false) ||
            !getString(command, "scope", scopeText, result, false)) {
            return false;
        }
        const auto definition = keywordDefinitionFromString(definitionText);
        const auto scope = keywordScopeFromString(scopeText);
        if (!definition || !scope) {
            return fail(result, kErrorInvalidValue, "unknown keyword definition or scope");
        }
        keyword.definition = *definition;
        keyword.scope = *scope;
        if (!getString(command, "category", keyword.category, result, false)) {
            return false;
        }
        if (command.contains("default")) {
            if (keyword.type == KeywordType::Boolean && command["default"].is_boolean()) {
                keyword.defaultValue = command["default"].get<bool>() ? 1 : 0;
            } else {
                int defaultValue = 0;
                if (!getInteger(command, "default", defaultValue, result)) {
                    return false;
                }
                keyword.defaultValue = defaultValue;
            }
        }
        if (command.contains("entries")) {
            if (!command["entries"].is_array()) {
                return fail(result, kErrorInvalidJson, "entries must be an array");
            }
            for (const Json& entryJson : command["entries"]) {
                if (!entryJson.is_object()) {
                    return fail(result, kErrorInvalidJson, "keyword entry must be an object");
                }
                KeywordEntry entry;
                if (!getInteger(entryJson, "id", entry.id, result) ||
                    !getString(entryJson, "displayName", entry.displayName, result) ||
                    !getString(entryJson, "referenceName", entry.referenceName, result)) {
                    return false;
                }
                keyword.entries.push_back(std::move(entry));
            }
        }
        std::string error;
        if (!state.graph.addKeyword(std::move(keyword), &error)) {
            return fail(result, kErrorNameConflict, error);
        }
        structural = true;
        return true;
    }

    if (op == "removeKeyword") {
        std::string refName;
        if (!getString(command, "refName", refName, result)) {
            return false;
        }
        if (state.graph.findKeyword(refName) == nullptr) {
            return fail(result, kEditorNotFound, "keyword not found: " + refName);
        }
        state.graph.removeKeyword(refName);
        std::vector<NodeId> nodes;
        for (const NodeInstance& node : state.graph.nodes) {
            if (node.type == "Keyword" && propertyRef(node) == refName) {
                nodes.push_back(node.id);
            }
        }
        for (NodeId id : nodes) {
            state.graph.removeNode(id);
            eraseLayoutNode(state, id);
            affected.insert(id);
        }
        structural = true;
        return true;
    }

    if (op == "setKeywordEntry") {
        std::string refName;
        std::string field;
        int entryId = 0;
        if (!getString(command, "refName", refName, result) ||
            !getString(command, "field", field, result) ||
            !getInteger(command, "entryId", entryId, result)) {
            return false;
        }
        Keyword* keyword = state.graph.findKeyword(refName);
        if (keyword == nullptr) {
            return fail(result, kEditorNotFound, "keyword not found: " + refName);
        }
        if (!command.contains("value")) {
            return fail(result, kErrorInvalidJson, "missing field: value");
        }
        const Json& value = command["value"];
        auto entry = std::find_if(keyword->entries.begin(), keyword->entries.end(),
                                  [entryId](const KeywordEntry& item) { return item.id == entryId; });
        if (entry == keyword->entries.end()) {
            keyword->entries.push_back({entryId, {}, {}});
            entry = std::prev(keyword->entries.end());
        }
        if (field == "id") {
            if (!value.is_number_integer() && !value.is_number_unsigned()) {
                return fail(result, kErrorInvalidJson, "id must be an integer");
            }
            entry->id = value.get<int>();
        } else if (field == "displayName" || field == "referenceName") {
            if (!value.is_string()) {
                return fail(result, kErrorInvalidJson, "field must be a string: " + field);
            }
            if (field == "displayName") {
                entry->displayName = value.get<std::string>();
            } else {
                entry->referenceName = value.get<std::string>();
            }
        } else {
            return fail(result, kErrorInvalidValue, "unknown keyword entry field: " + field);
        }
        structural = true;
        return true;
    }

    if (op == "addGroup") {
        Group group;
        if (command.contains("id") && !getInteger(command, "id", group.id, result)) {
            return false;
        } else if (!command.contains("id")) {
            group.id = state.graph.nextGroupId();
        }
        if (!getString(command, "title", group.title, result, false) ||
            !getVec2(command, "position", group.position, result, false) ||
            !getVec2(command, "size", group.size, result, false) ||
            !getColor(command, "color", group.color, result)) {
            return false;
        }
        for (const Group& existing : state.graph.groups) {
            if (existing.id == group.id) {
                return fail(result, kErrorDuplicateId, "group id already exists");
            }
        }
        if (command.contains("nodes") && command["nodes"].is_array()) {
            for (const Json& nodeId : command["nodes"]) {
                if (!nodeId.is_number_integer() && !nodeId.is_number_unsigned()) {
                    return fail(result, kErrorInvalidJson, "group nodes must contain integers");
                }
                group.nodes.push_back(nodeId.get<NodeId>());
            }
        }
        state.graph.groups.push_back(std::move(group));
        addMissingLayoutEntries(state);
        structural = true;
        return true;
    }

    if (op == "removeGroup") {
        int id = 0;
        if (!getInteger(command, "id", id, result)) {
            return false;
        }
        auto found = std::find_if(state.graph.groups.begin(), state.graph.groups.end(),
                                  [id](const Group& group) { return group.id == id; });
        if (found == state.graph.groups.end()) {
            return fail(result, kEditorNotFound, "group not found");
        }
        state.graph.groups.erase(found);
        eraseLayoutGroup(state, id);
        structural = true;
        return true;
    }

    if (op == "setGroup") {
        int id = 0;
        std::string field;
        if (!getInteger(command, "id", id, result) || !getString(command, "field", field, result)) {
            return false;
        }
        auto group = std::find_if(state.graph.groups.begin(), state.graph.groups.end(),
                                  [id](const Group& item) { return item.id == id; });
        if (group == state.graph.groups.end()) {
            return fail(result, kEditorNotFound, "group not found");
        }
        if (!command.contains("value")) {
            return fail(result, kErrorInvalidJson, "missing field: value");
        }
        const Json& value = command["value"];
        if (field == "title") {
            if (!value.is_string()) {
                return fail(result, kErrorInvalidJson, "title must be a string");
            }
            group->title = value.get<std::string>();
        } else if (field == "position" || field == "size") {
            if (!value.is_array() || value.size() != 2) {
                return fail(result, kErrorInvalidJson, "field must be a two-number array: " + field);
            }
            const Vec2 parsed{value[0].get<float>(), value[1].get<float>()};
            if (field == "position") {
                group->position = parsed;
            } else {
                group->size = parsed;
            }
            auto layoutGroup = std::find_if(state.layout.groups.begin(), state.layout.groups.end(),
                                            [id](const GroupLayout& item) { return item.id == id; });
            if (layoutGroup != state.layout.groups.end()) {
                if (field == "position") {
                    layoutGroup->position = parsed;
                } else {
                    layoutGroup->size = parsed;
                }
            }
        } else if (field == "color") {
            if (!value.is_array() || value.size() != 4) {
                return fail(result, kErrorInvalidJson, "color must be a four-number array");
            }
            group->color = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                            value[3].get<float>()};
        } else {
            return fail(result, kErrorInvalidValue, "unknown group field: " + field);
        }
        structural = true;
        return true;
    }

    if (op == "addNote") {
        Note note;
        if (command.contains("id") && !getInteger(command, "id", note.id, result)) {
            return false;
        } else if (!command.contains("id")) {
            note.id = state.graph.nextNoteId();
        }
        if (!getString(command, "title", note.title, result, false) ||
            !getString(command, "text", note.text, result, false) ||
            !getString(command, "theme", note.theme, result, false) ||
            !getVec2(command, "position", note.position, result, false) ||
            !getVec2(command, "size", note.size, result, false)) {
            return false;
        }
        for (const Note& existing : state.graph.notes) {
            if (existing.id == note.id) {
                return fail(result, kErrorDuplicateId, "note id already exists");
            }
        }
        state.graph.notes.push_back(std::move(note));
        addMissingLayoutEntries(state);
        structural = true;
        return true;
    }

    if (op == "setNote") {
        int id = 0;
        std::string field;
        if (!getInteger(command, "id", id, result) || !getString(command, "field", field, result)) {
            return false;
        }
        auto note = std::find_if(state.graph.notes.begin(), state.graph.notes.end(),
                                 [id](const Note& item) { return item.id == id; });
        if (note == state.graph.notes.end()) {
            return fail(result, kEditorNotFound, "note not found");
        }
        if (!command.contains("value")) {
            return fail(result, kErrorInvalidJson, "missing field: value");
        }
        const Json& value = command["value"];
        if (field == "title" || field == "text" || field == "theme") {
            if (!value.is_string()) {
                return fail(result, kErrorInvalidJson, "field must be a string: " + field);
            }
            if (field == "title") {
                note->title = value.get<std::string>();
            } else if (field == "text") {
                note->text = value.get<std::string>();
            } else {
                note->theme = value.get<std::string>();
            }
        } else if (field == "position" || field == "size") {
            if (!value.is_array() || value.size() != 2) {
                return fail(result, kErrorInvalidJson, "field must be a two-number array: " + field);
            }
            const Vec2 parsed{value[0].get<float>(), value[1].get<float>()};
            if (field == "position") {
                note->position = parsed;
            } else {
                note->size = parsed;
            }
            auto layoutNote = std::find_if(state.layout.notes.begin(), state.layout.notes.end(),
                                           [id](const NoteLayout& item) { return item.id == id; });
            if (layoutNote != state.layout.notes.end()) {
                if (field == "position") {
                    layoutNote->position = parsed;
                } else {
                    layoutNote->size = parsed;
                }
            }
        } else {
            return fail(result, kErrorInvalidValue, "unknown note field: " + field);
        }
        structural = true;
        return true;
    }

    if (op == "setTargetSetting") {
        std::string name;
        if (!getString(command, "name", name, result) || !command.contains("value")) {
            return false;
        }
        std::string error;
        auto parsed = detail::parseValue(command["value"], std::nullopt, &error);
        if (!parsed) {
            return fail(result, kErrorTypeMismatch, "invalid target setting: " + error);
        }
        state.graph.target.settings.insert_or_assign(name, std::move(*parsed));
        structural = true;
        return true;
    }

    if (op == "setLayout") {
        const Json& layoutJson = command.contains("layout") ? command["layout"] : command;
        LayoutLoadResult parsed = deserializeLayout(layoutJson.dump());
        if (!parsed.ok) {
            return fail(result, kErrorInvalidJson,
                        parsed.error.empty() ? "invalid layout" : parsed.error);
        }
        state.layout = std::move(parsed.layout);
        addMissingLayoutEntries(state);
        applyLayoutPositions(state);
        layoutChanged = true;
        return true;
    }

    if (op == "createSubgraph" || op == "createCustomFunction") {
        return fail(result, kErrorTargetUnsupported, "operation is not available before SG-6");
    }

    return fail(result, kErrorInvalidValue, "unknown command: " + op);
}

} // namespace
