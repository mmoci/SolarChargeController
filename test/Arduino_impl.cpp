#include "Arduino.h"
#include <gtest/gtest.h>

// Global instances
SerialClass Serial_instance;
SerialClass Serial = Serial_instance;

Serial2Class Serial2_instance;
Serial2Class Serial2 = Serial2_instance;

WireClass Wire_instance;
WireClass Wire = Wire_instance;

// Global milliseconds counter for tests
unsigned long g_millis_counter = 0;

// Helper functions for test time control
void advance_millis(unsigned long ms)
{
    g_millis_counter += ms;
}

void reset_millis()
{
    g_millis_counter = 0;
}

unsigned long millis()
{
    return g_millis_counter;
}

// Set the environment to allow use of WinMain (for Windows GUI)
#ifdef _WIN32
#include <windows.h>
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

//  Entry point for native Windows platform 
extern "C" int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    return main(__argc, __argv);
}
#endif

