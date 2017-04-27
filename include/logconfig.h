#ifndef __LOGCONFIG_H__
#define __LOGCONFIG_H__

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

#include "logger.h"

namespace logger {
enum Severity {
  NOTSET = 0,
  DEBUG = 10,
  INFO = 20,
  WARNING = 30,
  ERROR = 40,
  CRITICAL = 50
};

template <typename Stream>
void prop_level(Stream &o, const Line &l) {
  switch (l.info.level) {
    case 0: o << "NOTSET"; break;
    case 10: o << "DEBUG"; break;
    case 20: o << "INFO"; break;
    case 30: o << "WARNING"; break;
    case 40: o << "ERROR"; break;
    case 50: o << "CRITICAL"; break;
    default: o << l.info.level;
  }
}

// Can avoid the Line parameter in GCC 7 with template <auto>
template <typename Stream>
void prop_time(Stream &o, const Line &l) {
  using std::chrono::system_clock;
  auto now = system_clock::to_time_t(system_clock::now());
  o << std::put_time(std::localtime(&now), "%T");
}

template <typename Stream>
void prop_date(Stream &o, const Line &l) {
  using std::chrono::system_clock;
  auto now = system_clock::to_time_t(system_clock::now());
  o << std::put_time(std::localtime(&now), "%F");
}

template <typename Stream>
void prop_thread(Stream &o, const Line &l) {
  auto f = o.flags();
  o << std::hex << std::showbase << std::this_thread::get_id();
  o.flags(f);
}

constexpr const char full_fmt[] = "\033[1;31m[% %]\033[0m %[Thread %:%(%:%)]: [%] [%]";
template <typename Stream>
using FullLogger =
  Logger<0, Stream, full_fmt, prop_date, prop_time, prop_level, prop_thread, prop_file,
  prop_func, prop_line, prop_msg, prop_hash>;

constexpr const char basic_fmt[] = "%";
template <typename Stream>
using BasicLogger = Logger<INFO, Stream, basic_fmt, prop_msg>;
}

logger::FullLogger<std::ostream> LOG(std::clog);
#define LOG(severity) CLOG(LOG, severity)
#define CLOG(instance, severity) CLOGL(instance, logger::severity)
#define CLOGL(instance, severity)                                              \
  instance.log<severity>({__FILE__, __func__, __LINE__, severity})

#endif /* __LOGCONFIG_H__ */
