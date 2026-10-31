#include <map>
#include <string>
#include <unity.h>
#include "reading_log.h"

struct MemoryStore {
  std::map<std::string, std::string> files;

  std::string read(const char *path) {
    return files[path];
  }
  void write(const char *path, const std::string &text) {
    files[path] = text;
  }
};

void setUp() {}
void tearDown() {}

void chart_rows_are_empty_with_no_readings() {
  ReadingLog<3> log;

  TEST_ASSERT_EQUAL_STRING("[]", log.chartRows().c_str());
}

void chart_row_holds_one_complete_reading() {
  ReadingLog<3> log;
  log.append("2026,10-1,31,12,00,00", 7, 400, 68);

  TEST_ASSERT_EQUAL_STRING("[[new Date(2026,10-1,31,12,00,00),7.00,400.00,68.00],\n]", log.chartRows().c_str());
}

void full_log_drops_oldest_reading_from_every_series() {
  ReadingLog<2> log;
  log.append("t1", 1, 10, 100);
  log.append("t2", 2, 20, 200);
  log.append("t3", 3, 30, 300);

  TEST_ASSERT_EQUAL_STRING("[[new Date(t2),2.00,20.00,200.00],\n[new Date(t3),3.00,30.00,300.00],\n]", log.chartRows().c_str());
  TEST_ASSERT_EQUAL_FLOAT(25, log.ec().mean);
}

void saved_log_restores_timestamps_and_statistics() {
  MemoryStore store;
  ReadingLog<3> saved;
  saved.append("t1", 7.01, 10, 100);
  saved.append("t2", 7.03, 20, 200);
  saved.append("t3", 7.02, 30, 300);
  saved.append("t4", 7.04, 40, 400);
  saved.save(store);

  ReadingLog<3> restored;
  restored.restore(store);

  TEST_ASSERT_EQUAL_STRING(saved.chartRows().c_str(), restored.chartRows().c_str());
  TEST_ASSERT_EQUAL_FLOAT(30, restored.ec().mean);
  TEST_ASSERT_EQUAL_FLOAT(40, restored.ec().currentValue);
  TEST_ASSERT_FLOAT_WITHIN(0.00002, 0.0001, restored.ph().sVar);
}

void reading_without_a_timestamp_survives_a_restart_in_position() {
  MemoryStore store;
  ReadingLog<3> saved;
  saved.append("", 1, 10, 100);
  saved.append("t2", 2, 20, 200);
  saved.save(store);

  ReadingLog<3> restored;
  restored.restore(store);

  TEST_ASSERT_EQUAL_STRING("[[new Date(),1.00,10.00,100.00],\n[new Date(t2),2.00,20.00,200.00],\n]", restored.chartRows().c_str());
}

void save_cut_short_keeps_every_complete_reading() {
  MemoryStore store;
  store.files["/readings.txt"] = "readings v1\nt1 1 10 100\nt2 2 20 200\nt3 3 3";

  ReadingLog<5> log;
  log.restore(store);

  TEST_ASSERT_EQUAL_INT(2, log.size());
  TEST_ASSERT_EQUAL_FLOAT(2, log.ph().currentValue);
}

void cleared_log_does_not_bring_back_old_format_files() {
  MemoryStore store;
  store.files["/phdata.txt"] = "phdata 1 7.00 0.00 0.00 7.00 0 7.00";
  store.files["/ecdata.txt"] = "ecdata 1 400.00 0.00 0.00 400.00 0 400.00";
  store.files["/wtdata.txt"] = "wtdata 1 68.00 0.00 0.00 68.00 0 68.00";
  store.files["/datetime.txt"] = "t1 |";
  ReadingLog<3> log;
  log.restore(store);
  log.clear();
  log.save(store);

  ReadingLog<3> restored;
  restored.restore(store);

  TEST_ASSERT_EQUAL_INT(0, restored.size());
}

void old_format_files_migrate_in_chronological_order() {
  MemoryStore store;
  store.files["/phdata.txt"] = "phdata 1 3.00 2.00 1.00 4.00 1 4.00,2.00,3.00";
  store.files["/ecdata.txt"] = "ecdata 1 30.00 200.00 100.00 40.00 1 40.00,20.00,30.00";
  store.files["/wtdata.txt"] = "wtdata 1 300.00 20000.00 10000.00 400.00 1 400.00,200.00,300.00";
  store.files["/datetime.txt"] = "t4 |t2 |t3 |";

  ReadingLog<3> log;
  log.restore(store);

  TEST_ASSERT_EQUAL_STRING("[[new Date(t2),2.00,20.00,200.00],\n[new Date(t3),3.00,30.00,300.00],\n[new Date(t4),4.00,40.00,400.00],\n]", log.chartRows().c_str());
  TEST_ASSERT_EQUAL_FLOAT(3, log.ph().mean);
}

void misaligned_old_files_keep_newest_readings_that_line_up() {
  MemoryStore store;
  store.files["/phdata.txt"] = "phdata 1 7.00 0.00 0.00 7.00 0 7.00";
  store.files["/ecdata.txt"] = "ecdata 2 15.00 50.00 50.00 20.00 0 10.00,20.00";
  store.files["/wtdata.txt"] = "wtdata 2 150.00 5000.00 5000.00 200.00 0 100.00,200.00";
  store.files["/datetime.txt"] = "t1 |";

  ReadingLog<3> log;
  log.restore(store);

  TEST_ASSERT_EQUAL_STRING("[[new Date(t1),7.00,20.00,200.00],\n]", log.chartRows().c_str());
}

void missing_files_start_an_empty_log() {
  MemoryStore store;
  ReadingLog<3> log;
  log.restore(store);

  TEST_ASSERT_EQUAL_INT(0, log.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(chart_rows_are_empty_with_no_readings);
  RUN_TEST(chart_row_holds_one_complete_reading);
  RUN_TEST(full_log_drops_oldest_reading_from_every_series);
  RUN_TEST(saved_log_restores_timestamps_and_statistics);
  RUN_TEST(reading_without_a_timestamp_survives_a_restart_in_position);
  RUN_TEST(save_cut_short_keeps_every_complete_reading);
  RUN_TEST(cleared_log_does_not_bring_back_old_format_files);
  RUN_TEST(old_format_files_migrate_in_chronological_order);
  RUN_TEST(misaligned_old_files_keep_newest_readings_that_line_up);
  RUN_TEST(missing_files_start_an_empty_log);
  return UNITY_END();
}
