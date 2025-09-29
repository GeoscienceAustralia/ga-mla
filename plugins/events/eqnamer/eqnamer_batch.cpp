#include "eqnamer.cpp"
#include "seiscomp/config/config.h"
#include "seiscomp/datamodel/types.h"
#include <iostream>
#include <ostream>
#include <seiscomp/datamodel/event.h>
#include <seiscomp/datamodel/origin.h>

using Seiscomp::DataModel::Origin;
using Seiscomp::DataModel::EvaluationStatus;
using Seiscomp::DataModel::REVIEWED;
using std::cout;
using std::endl;
using std::cerr;

class TestEQNamer : EQNamer {
public:
    void test()
    {
        Seiscomp::Config::Config cfg;
        cfg.readConfig("/opt/seiscomp/etc/scevent.cfg");
        cerr << "citiesPath: " << cfg.getString("eqnamer.citiesPath") << endl;

        _setup(cfg);
        cout << "longitude,latitude,name" << endl;
        for (double x = -179; x < 180; x += 1) {
            for (double y = 80; y > -80; y -= 1) {
                Origin o("mypublicidsux");
                o.setLongitude(x);
                o.setLatitude(y);
                o.setEvaluationStatus(EvaluationStatus(REVIEWED));
                auto name = nameOrigin(&o, "testorigin");
                cout << x << "," << y << ",\"" << name << "\"" << endl;
            }
        }
    }
};

int main(int argc, char** argv)
{
    TestEQNamer().test();
    return 0;
}
