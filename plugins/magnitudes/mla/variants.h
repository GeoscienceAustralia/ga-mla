#ifndef __MLA_PLUGIN_VARIANTS_H__
#define __MLA_PLUGIN_VARIANTS_H__
#include "mla.h"

#define _MLA_VARIANT(AMPCLASS, MAGCLASS, NAME, DEFAULTFILTER) \
    class AMPCLASS : public Amplitude_MLA { \
        DECLARE_SC_CLASS(AMPCLASS); \
    public: \
        AMPCLASS() : Amplitude_MLA(#NAME) {}; \
    protected: \
        std::string defaultFilter() const override { return DEFAULTFILTER; }; \
    }; \
    class MAGCLASS : public Magnitude_MLA { \
    public: \
        MAGCLASS() : Magnitude_MLA(#NAME) {}; \
        std::string amplitudeType() const override { return #NAME; }; \
    }

#define DEFINE_MLA_VARIANT(NAME, DEFAULTFILTER) \
    _MLA_VARIANT(Amplitude_##NAME, Magnitude_##NAME, NAME, DEFAULTFILTER)


DEFINE_MLA_VARIANT(MLa01, "BW_HP(3, 0.1)");
DEFINE_MLA_VARIANT(MLa05, "BW_HP(3, 0.5)");
DEFINE_MLA_VARIANT(MLa075, "BW_HP(3, 0.75)");

#endif /* __MLA_PLUGIN_VARIANTS_H__ */
