#include "hbg/analysis/export_csv.h"
#include "hbg/analysis/report_files.h"

#include "test_report_fixtures.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace hbg::analysis;
using namespace hbg_test;

namespace {

std::vector<std::vector<std::string>> parseCsvRecords(const std::string& text) {
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char character = text[i];
        if (quoted) {
            if (character == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(character);
            }
        } else if (character == '"') {
            quoted = true;
        } else if (character == ';') {
            fields.push_back(field);
            field.clear();
        } else if (character == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            fields.push_back(field);
            field.clear();
            records.push_back(fields);
            fields.clear();
            ++i;
        } else {
            field.push_back(character);
        }
    }
    return records;
}

} // namespace

TEST_CASE("CSV uses the planned columns and quoting rules") {
    VariantReport variant =
        makeVariant("aaaaaaaaaaaa", {"B=2", "A=1"}, 12, 500, 4);
    variant.compileMs = 10;
    variant.metrics->textureCount = 2;
    variant.metrics->samplerCount = 1;

    ReportSummary summary =
        makeSummary("dxbc", "shaders/a\"b.fs.sc", "fragment", {variant});
    const std::string csv = buildCsv({summary});

    CHECK(csv.rfind("\"Source\";", 0) == 0);
    CHECK(csv.find("\"CompileMs\";") != std::string::npos);
    CHECK(csv.find("\"ErrorCount\";\r\n") != std::string::npos);
    CHECK(csv.find("\"shaders/a\"\"b.fs.sc\";") != std::string::npos);
    CHECK(csv.find("\"A=1\r\nB=2\";") != std::string::npos);

    const std::vector<std::vector<std::string>> records = parseCsvRecords(csv);
    REQUIRE(records.size() == 2);
    const std::vector<std::string>& header = records[0];
    REQUIRE(header.size() == 24);
    CHECK(header[0] == "Source");
    CHECK(header[5] == "Status");
    CHECK(header[21] == "WarningCount");
    CHECK(header[22] == "ErrorCount");
    CHECK(header[23] == "");

    const std::vector<std::string>& row = records[1];
    REQUIRE(row.size() == 24);
    CHECK(row[0] == "shaders/a\"b.fs.sc");
    CHECK(row[1] == "fragment");
    CHECK(row[2] == "dxbc");
    CHECK(row[3] == "aaaaaaaaaaaa");
    CHECK(row[4] == "A=1\r\nB=2");
    CHECK(row[5] == "ok");
    CHECK(row[6] == "10");
    CHECK(row[7] == "500");
    CHECK(row[8] == "12");
    CHECK(row[9] == "5");
    CHECK(row[10] == "4");
    CHECK(row[11].empty());
    CHECK(row[12].empty());
    CHECK(row[13] == "2");
    CHECK(row[14] == "1");
    CHECK(row[15].empty());
    CHECK(row[16].empty());
    CHECK(row[17].empty());
    CHECK(row[18].empty());
    CHECK(row[19].empty());
    CHECK(row[20].empty());
    CHECK(row[21] == "0");
    CHECK(row[22] == "0");
}

TEST_CASE("CSV export accepts report directories and index files") {
    TempDirectory temp;
    const fs::path reportsDir = temp.subdirectory("reports");
    const ReportSummary summary =
        makeSummary("dxbc", "shaders/passes/forward.fs.sc", "fragment",
                    {makeVariant("aaaaaaaaaaaa", {"A=1"}, 12, 500)});
    writeSummaryTree(reportsDir, "dxbc/forward.fs.json", summary);
    writeReportIndex(reportsDir);

    const fs::path fromDirectory = temp.subdirectory("from-directory.csv");
    const fs::path fromIndex = temp.subdirectory("from-index.csv");
    CHECK(exportCsv(reportsDir, fromDirectory) == 1);
    CHECK(exportCsv(reportsDir / "index.json", fromIndex) == 1);
    CHECK(readRawFile(fromDirectory) == buildCsv({summary}));
    CHECK(readRawFile(fromIndex) == buildCsv({summary}));
}
