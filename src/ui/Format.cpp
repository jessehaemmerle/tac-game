#include "ui/Format.h"

#include <cmath>
#include <cstdio>

#include "core/Localization.h"
#include "core/Math.h"

namespace it::ui {

std::string fmtInt(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v), out;
    int n = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (n && n % 3 == 0) out.insert(out.begin(), loc::language() == "de" ? '.' : ',');
        out.insert(out.begin(), *it);
        ++n;
    }
    return v < 0 ? "-" + out : out;
}

std::string fmtRange(double m, bool metric) {
    char b[64];
    if (metric) {
        if (m < 2000.0) std::snprintf(b, sizeof(b), "%s m", fmtInt(std::llround(m / 10.0) * 10).c_str());
        else std::snprintf(b, sizeof(b), "%.1f km", m / 1000.0);
    } else {
        double yd = m / 0.9144;
        if (yd < 20000.0) std::snprintf(b, sizeof(b), "%s yd", fmtInt(std::llround(yd / 100.0) * 100).c_str());
        else std::snprintf(b, sizeof(b), "%.1f nm", m / 1852.0);
    }
    std::string s = b;
    if (loc::language() == "de")
        for (auto& c : s)
            if (c == '.' && (s.find(" km") != std::string::npos || s.find(" nm") != std::string::npos)) c = ',';
    return s;
}

std::string fmtRangeShort(double m, bool metric) {
    char b[32];
    if (metric) std::snprintf(b, sizeof(b), "%.1fkm", m / 1000.0);
    else std::snprintf(b, sizeof(b), "%.1fnm", m / 1852.0);
    return b;
}

std::string fmtBearing(double deg) {
    char b[16];
    std::snprintf(b, sizeof(b), "%03d\xC2\xB0", int(std::lround(wrap360(deg))) % 360);
    return b;
}

std::string fmtClock(double h) {
    h = std::fmod(std::fmod(h, 24.0) + 24.0, 24.0);
    int hh = int(h), mm = int((h - hh) * 60.0);
    char b[16];
    std::snprintf(b, sizeof(b), "%02d:%02d", hh, mm);
    return b;
}

std::string fmtDuration(double s) {
    int t = int(std::max(0.0, s));
    char b[32];
    if (t >= 3600) std::snprintf(b, sizeof(b), "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    else std::snprintf(b, sizeof(b), "%d:%02d", t / 60, t % 60);
    return b;
}

std::string fmtTonnes(double t) { return fmtInt(std::llround(t)) + " t"; }
std::string fmtKnots(double kn) {
    char b[16];
    std::snprintf(b, sizeof(b), "%.0f kn", kn);
    return b;
}
std::string fmtPercent(double f) {
    char b[16];
    std::snprintf(b, sizeof(b), "%d %%", int(std::lround(f * 100.0)));
    return b;
}

const char* sideName(sim::Side s) {
    switch (s) {
        case sim::Side::Allies: return tr("side.allies");
        case sim::Side::Axis: return tr("side.axis");
        default: return tr("side.neutral");
    }
}

const char* nationName(sim::Nation n) {
    switch (n) {
        case sim::Nation::USN: return tr("nation.usn");
        case sim::Nation::RAN: return tr("nation.ran");
        case sim::Nation::RN: return tr("nation.rn");
        case sim::Nation::IJN: return tr("nation.ijn");
        case sim::Nation::KM: return tr("nation.km");
        case sim::Nation::RM: return tr("nation.rm");
        case sim::Nation::MN: return tr("nation.mn");
        default: return tr("nation.unknown");
    }
}

const char* shipTypeName(sim::ShipType t) {
    switch (t) {
        case sim::ShipType::BB: return tr("type.bb");
        case sim::ShipType::BC: return tr("type.bc");
        case sim::ShipType::CV: return tr("type.cv");
        case sim::ShipType::CA: return tr("type.ca");
        case sim::ShipType::CL: return tr("type.cl");
        case sim::ShipType::CLAA: return tr("type.claa");
        case sim::ShipType::DD: return tr("type.dd");
        case sim::ShipType::DE: return tr("type.de");
        case sim::ShipType::TB: return tr("type.tb");
        case sim::ShipType::PT: return tr("type.pt");
        case sim::ShipType::SS: return tr("type.ss");
        case sim::ShipType::AP: return tr("type.ap");
        case sim::ShipType::AK: return tr("type.ak");
        case sim::ShipType::AO: return tr("type.ao");
        default: return tr("type.unknown");
    }
}

const char* shipTypeAbbrev(sim::ShipType t) {
    switch (t) {
        case sim::ShipType::BB: return "BB";
        case sim::ShipType::BC: return "BC";
        case sim::ShipType::CV: return "CV";
        case sim::ShipType::CA: return "CA";
        case sim::ShipType::CL: return "CL";
        case sim::ShipType::CLAA: return "CLAA";
        case sim::ShipType::DD: return "DD";
        case sim::ShipType::DE: return "DE";
        case sim::ShipType::TB: return "TB";
        case sim::ShipType::PT: return "PT";
        case sim::ShipType::SS: return "SS";
        case sim::ShipType::AP: return "AP";
        case sim::ShipType::AK: return "AK";
        case sim::ShipType::AO: return "AO";
        default: return "?";
    }
}

const char* crewName(sim::CrewQuality c) {
    switch (c) {
        case sim::CrewQuality::Green: return tr("crew.green");
        case sim::CrewQuality::Regular: return tr("crew.regular");
        case sim::CrewQuality::Veteran: return tr("crew.veteran");
        case sim::CrewQuality::Elite: return tr("crew.elite");
    }
    return "";
}

}  // namespace it::ui
