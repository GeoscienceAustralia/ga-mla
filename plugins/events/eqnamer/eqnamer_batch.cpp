#define SEISCOMP_COMPONENT "eqnamer-batch-test"

#include "eqnamer.cpp"
#include "seiscomp/config/config.h"
#include "seiscomp/datamodel/types.h"
#include <seiscomp/datamodel/origin.h>

#include <seiscomp/logging/output/fd.h>

#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

#include "rapidcsv.h"

using Seiscomp::DataModel::Origin;
using Seiscomp::DataModel::EvaluationStatus;
using Seiscomp::DataModel::REVIEWED;
using Seiscomp::DataModel::PRELIMINARY;

class TestEQNamer : public EQNamer {
public:
  void test() {
    Seiscomp::Config::Config cfg;
    cfg.readConfig("/opt/seiscomp/etc/scevent.cfg");
    _setup(cfg);

    rapidcsv::Document doc(std::cin, rapidcsv::LabelParams(0, -1));

    auto lons = doc.GetColumn<double>("longitude");
    auto lats = doc.GetColumn<double>("latitude");

    std::vector<std::string> initialNames;
    std::vector<std::string> finalNames;
    initialNames.reserve(lons.size());
    finalNames.reserve(lons.size());

    for (size_t i = 0; i < lons.size(); ++i) {
      Origin o("stdin-row");
      o.setLongitude(lons[i]);
      o.setLatitude(lats[i]);

      o.setEvaluationStatus(EvaluationStatus(PRELIMINARY));
      initialNames.push_back(nameOrigin(&o, "batch"));

      o.setEvaluationStatus(EvaluationStatus(REVIEWED));
      finalNames.push_back(nameOrigin(&o, "batch"));
    }

    doc.InsertColumn(doc.GetColumnNames().size(), initialNames, "initialRegionName");
    doc.InsertColumn(doc.GetColumnNames().size(), finalNames, "finalRegionName");
    doc.Save(std::cout);
  }
};

int main() {
  Seiscomp::Logging::FdOutput stderrLog(STDERR_FILENO);
  stderrLog.subscribe(Seiscomp::Logging::_SCDebugChannel);
  stderrLog.subscribe(Seiscomp::Logging::_SCInfoChannel);
  stderrLog.subscribe(Seiscomp::Logging::_SCNoticeChannel);
  stderrLog.subscribe(Seiscomp::Logging::_SCWarningChannel);
  stderrLog.subscribe(Seiscomp::Logging::_SCErrorChannel);

  TestEQNamer().test();
  return 0;
}
