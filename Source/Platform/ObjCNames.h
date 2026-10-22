#pragma once

// Objective-C classes are process-global: the AU and the VST3 (and two
// ChordEngine versions) can be loaded into one host at the same time, so every
// class name carries a per-binary suffix. CMake defines
// CHORDENGINE_OBJC_SUFFIX per bundle (format + version), the way native VYRE
// does it.
#ifndef CHORDENGINE_OBJC_SUFFIX
 #define CHORDENGINE_OBJC_SUFFIX Dev
#endif
#define CHORDENGINE_OBJC_CAT2(a, b) a##_##b
#define CHORDENGINE_OBJC_CAT(a, b) CHORDENGINE_OBJC_CAT2(a, b)
#define CE_OBJC(name) CHORDENGINE_OBJC_CAT(name, CHORDENGINE_OBJC_SUFFIX)
#define CHORDENGINE_OBJC_STR2(x) #x
#define CHORDENGINE_OBJC_STR(x) CHORDENGINE_OBJC_STR2(x)
