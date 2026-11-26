#include "migrate.h"

#include "sg/graph.h"

namespace sg {
namespace detail {

bool migrateJson(Json& root, std::vector<Diagnostic>& diagnostics, std::string& error) {
    if (!root.is_object()) {
        error = "graph root must be an object";
        return false;
    }
    int version = 0;
    if (root.contains("schemaVersion")) {
        if (!root["schemaVersion"].is_number_integer()) {
            error = "schemaVersion must be an integer";
            return false;
        }
        version = root["schemaVersion"].get<int>();
    }
    if (version > Graph::kCurrentSchemaVersion) {
        error = "graph schemaVersion is newer than supported";
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorSchemaVersion;
        diagnostic.message = error;
        diagnostics.push_back(std::move(diagnostic));
        return false;
    }
    if (version < Graph::kCurrentSchemaVersion) {
        if (version < 0) {
            error = "invalid negative schemaVersion";
            return false;
        }
        root["schemaVersion"] = Graph::kCurrentSchemaVersion;
        if (!root.contains("kind")) {
            root["kind"] = "graph";
        }
        if (!root.contains("target") &&
            (!root.contains("kind") || root["kind"] != "subgraph")) {
            root["target"] = {{"id", "lit"}, {"subTarget", "lit"}, {"settings", Json::object()}};
        }
        for (const char* key : {"properties", "keywords", "categories", "nodes", "edges", "groups", "notes"}) {
            if (!root.contains(key)) {
                root[key] = Json::array();
            }
        }
        if (root["nodes"].is_array()) {
            for (Json& node : root["nodes"]) {
                if (!node.is_object()) {
                    continue;
                }
                if (!node.contains("typeVersion")) {
                    node["typeVersion"] = 1;
                }
                if (!node.contains("position")) {
                    node["position"] = Json::array({0.0f, 0.0f});
                }
                if (!node.contains("settings")) {
                    node["settings"] = Json::object();
                }
                if (!node.contains("slots")) {
                    node["slots"] = Json::object();
                }
            }
        }
    }
    return true;
}

} // namespace detail
} // namespace sg
