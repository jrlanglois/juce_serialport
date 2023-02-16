#include "juce_serialport.h"

#if JUCE_ANDROID
    #include <juce_core/native/juce_android_JNIHelpers.h>
#endif

#include "native/juce_serialport_Android.cpp"
#include "native/juce_serialport_Windows.cpp"

