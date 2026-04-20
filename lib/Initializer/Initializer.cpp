#include "Initializer.h"
#include "InitializerDps.h"
#include "InitializerPwm.h"

Initializer& Initializer::getInstance()
{
#ifdef DPS_DC_CONVERTER
    static InitializerDps instance{};
#else
    static InitializerPwm instance{};
#endif
    return instance;
}