#include "eqnamer.cpp"
#include "seiscomp/config/config.h"
#include "seiscomp/datamodel/types.h"
#include <seiscomp/datamodel/origin.h>

#include <iostream>
#include <string>
#include <vector>

#include "rapidcsv.h"

using Seiscomp::DataModel::Origin;
using Seiscomp::DataModel::EvaluationStatus;
using Seiscomp::DataModel::REVIEWED;
using Seiscomp::DataModel::PRELIMINARY;

class TestEQNamer : EQNamer {
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
  TestEQNamer().test();
  return 0;
}
