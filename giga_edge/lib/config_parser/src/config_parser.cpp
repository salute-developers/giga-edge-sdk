#include "giga_edge/lib/config_parser/config_parser.h"

#include <algorithm>
#include <set>
#include <streambuf>
#include <thread>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <vector>
#include <windows.h>
#elif defined(__linux__)
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace giga_edge {

namespace {

struct MemBuf : std::streambuf {
  MemBuf(const char* data, size_t size) {
    char* t = const_cast<char*>(data);
    setg(t, t, t + size);
  }
};

#if defined(__linux__)
std::set<int> ParseCpuList(std::istream& list) {
  std::set<int> cpus;
  std::string range;
  while (std::getline(list, range, ',')) {
    int first = 0;
    int last = 0;
    char dash = 0;
    std::istringstream in(range);
    if (!(in >> first)) {
      continue;
    }
    last = (in >> dash >> last) ? last : first;
    for (int cpu = first; cpu <= last; ++cpu) {
      cpus.insert(cpu);
    }
  }
  return cpus;
}

std::set<int> ReadCpuList(const std::string& path) {
  std::ifstream file(path);
  return ParseCpuList(file);
}

// Cpus_allowed_list instead of sched_getaffinity: the latter reports only the cores active right now, and an idle
// Android puts almost all of them to sleep (core_ctl) and wakes them under load within milliseconds. The list
// shrinks only when cores are really taken away: taskset, a container cpuset, the firmware on overheating.
std::set<int> AllowedCpus() {
  std::ifstream status("/proc/thread-self/status");
  for (std::string line; std::getline(status, line);) {
    if (line.rfind("Cpus_allowed_list:", 0) == 0) {
      std::istringstream list(line.substr(sizeof("Cpus_allowed_list:") - 1));
      return ParseCpuList(list);
    }
  }
  return {};
}

int ReadInt(const std::string& path) {
  int value = -1;
  std::ifstream(path) >> value;
  return value;
}
#endif

// Hyperthreads and efficiency cores only slow ORT and llama.cpp down: the work is split evenly,
// and everyone waits for the slowest one.
int PerformanceCoreCount() {
#if defined(__APPLE__)
  for (const char* name : {"hw.perflevel0.physicalcpu", "hw.physicalcpu"}) {
    int value = 0;
    size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, nullptr, 0) == 0 && value > 0) {
      return value;
    }
  }
#elif defined(_WIN32)
  DWORD size = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
  std::vector<char> buffer(size);
  auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
  if (size > 0 && GetLogicalProcessorInformationEx(RelationProcessorCore, info, &size)) {
    std::vector<BYTE> classes;
    for (DWORD offset = 0; offset < size;) {
      const auto* core = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
      classes.push_back(core->Processor.EfficiencyClass);
      offset += core->Size;
    }
    if (!classes.empty()) {
      const auto top = *std::max_element(classes.begin(), classes.end());
      return static_cast<int>(std::count(classes.begin(), classes.end(), top));
    }
  }
#elif defined(__linux__)
  // On hybrid Intel CPUs the performance cores are listed in cpu_core.
  auto cpus = ReadCpuList("/sys/devices/cpu_core/cpus");
  if (cpus.empty()) {
    cpus = ReadCpuList("/sys/devices/system/cpu/online");
  }
  // taskset and a container cpuset: count only the CPUs the process may use.
  if (const auto allowed = AllowedCpus(); !allowed.empty()) {
    for (auto it = cpus.begin(); it != cpus.end();) {
      it = allowed.count(*it) != 0 ? std::next(it) : cpus.erase(it);
    }
  }
  // big.LITTLE on ARM: little cores are 2-4 times weaker than big ones; on x86 and server ARM the capacities are
  // equal or the file is missing.
  int top_capacity = 0;
  for (const int cpu : cpus) {
    top_capacity = std::max(
      top_capacity, ReadInt("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpu_capacity")
    );
  }
  for (auto it = cpus.begin(); top_capacity > 0 && it != cpus.end();) {
    const int capacity = ReadInt("/sys/devices/system/cpu/cpu" + std::to_string(*it) + "/cpu_capacity");
    it = (capacity >= 0 && capacity * 2 < top_capacity) ? cpus.erase(it) : std::next(it);
  }
  std::set<std::pair<int, int> > cores;
  for (const int cpu : cpus) {
    const std::string topology = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/";
    // Without the topology files all cores would merge into one (-1, -1) and auto would give a single thread.
    if (const int core = ReadInt(topology + "core_id"); core >= 0) {
      cores.emplace(ReadInt(topology + "physical_package_id"), core);
    }
  }
  if (!cores.empty()) {
    return static_cast<int>(cores.size());
  }
#endif
  return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

} // namespace

int AllowedCpuCount() {
#if defined(__linux__)
  return static_cast<int>(AllowedCpus().size());
#else
  return 0;
#endif
}

int ParseThreadCount(const YAML::Node& node, int fallback) {
  if (node && node.IsScalar() && node.as<std::string>() == "auto") {
    static const int cores = PerformanceCoreCount();
    return cores;
  }
  return node.as<int>(fallback);
}

YAML::Node ParseYaml(const char* data, size_t size) {
  MemBuf buf(data, size);
  std::istream istr(&buf);
  return YAML::Load(istr);
}

} // namespace giga_edge
