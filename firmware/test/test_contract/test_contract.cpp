// Wire-contract test: runs the exact apicore::shape fillers that build every
// production reply, writes the serialized JSON to spec/v2/fixtures/generated/,
// and CI validates those files against spec/v2/schemas (validate.mjs). A C++
// key edit without a schema update — or vice versa — fails CI.
//
// Host-only (pio test -e native -d firmware); never runs on the device.
#include <unity.h>

#include <ArduinoJson.h>
#include <filesystem>
#include <fstream>
#include <string>

#include "api_core.h"

namespace fs = std::filesystem;

// Repo root relative to the test's working directory (PlatformIO runs tests
// from the project dir, firmware/; direct invocation may use the repo root).
static fs::path repoRoot() {
  for (const char *root : {"..", "."}) {
    if (fs::exists(fs::path(root) / "spec" / "v2" / "schemas")) return root;
  }
  TEST_FAIL_MESSAGE("cannot locate spec/v2/schemas from cwd");
  return {};
}

static void writeFixture(const char *stem, const char *name, const JsonDocument &doc) {
  fs::path dir = repoRoot() / "spec" / "v2" / "fixtures" / "generated" / stem;
  fs::create_directories(dir);
  std::string out;
  serializeJson(doc, out);
  TEST_ASSERT_GREATER_THAN_MESSAGE(2, (int)out.size(), stem);
  std::ofstream f(dir / (std::string(name) + ".json"));
  f << out << "\n";
  TEST_ASSERT_TRUE_MESSAGE(f.good(), stem);
}

void test_spec_reply() {
  JsonDocument d;
  apicore::shape::spec(d);
  TEST_ASSERT_EQUAL(2, d["protocol"].as<int>());
  TEST_ASSERT_TRUE(d["ok"].as<bool>());
  writeFixture("spec.reply", "ok", d);
}

void test_read_reply() {
  JsonDocument d;
  apicore::shape::read(d, {"04A1B2C3", true, "01001", "1200F6", "1KG", "000001"});
  writeFixture("read.reply", "ok", d);
}

void test_dump_reply() {
  JsonDocument d;
  apicore::shape::dump(
      d, {"04A1B2C3", true,
          "AB1240276A21010010C12E1F01650000010000000000000000000000000000000000000000000000"
          "0000000000000000"});
  writeFixture("dump.reply", "ok", d);
}

void test_status_reply() {
  JsonDocument idle;
  apicore::shape::status(idle, {false, "sta", "192.168.1.50", {false, false, false, "", ""}});
  writeFixture("status.reply", "idle", idle);

  JsonDocument done;
  apicore::shape::status(done, {false, "ap", "192.168.4.1", {true, true, true, "04A1B2C3", ""}});
  writeFixture("status.reply", "write-done", done);
}

void test_config_reply() {
  JsonDocument d;
  apicore::shape::configGet(d, {"MyWifi", "SpoolID", "spoolid", 3, 115200});
  writeFixture("config.reply", "get", d);
}

void test_simple_replies() {
  JsonDocument staged;
  apicore::shape::writeStaged(staged);
  writeFixture("write.reply", "staged", staged);

  JsonDocument reboot;
  apicore::shape::ackReboot(reboot);
  writeFixture("configset.reply", "ok", reboot);
  writeFixture("ota.end.reply", "ok", reboot);

  JsonDocument wrote;
  apicore::shape::written(wrote, 4096);
  writeFixture("db.data.reply", "ok", wrote);
  writeFixture("ota.data.reply", "ok", wrote);
}

void test_every_error_code() {
  // One fixture per stable code; err-*.json validates against common ErrorReply.
  static const char *codes[] = {"bad_json", "invalid_params", "no_tag",
                                "db_failed", "ota_failed", "size_required",
                                "bad_chunk", "unknown_cmd", "internal"};
  for (const char *code : codes) {
    JsonDocument d;
    apicore::shape::error(d, code, "sample message");
    TEST_ASSERT_FALSE(d["ok"].as<bool>());
    writeFixture("errors", (std::string("err-") + code).c_str(), d);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_spec_reply);
  RUN_TEST(test_read_reply);
  RUN_TEST(test_dump_reply);
  RUN_TEST(test_status_reply);
  RUN_TEST(test_config_reply);
  RUN_TEST(test_simple_replies);
  RUN_TEST(test_every_error_code);
  return UNITY_END();
}
