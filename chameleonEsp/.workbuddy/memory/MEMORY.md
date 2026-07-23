# chameleonEsp Project Notes

## Project Type
UE5 game cheat/ESP DLL (x64, v143 toolset, C++20)

## SDK Architecture
- Dumper-7 generated SDK from game binary
- Assertions.inl: ~26MB file with static_assert macros for SDK class offsets
- When adding new SDK classes (not in original dump), must add empty #define fallback macros for their DUMPER7_ASSERTS_* names in Assertions.inl (after SDK_NAMESPACE_END)

## Key Fixes Applied (2026-07-23)
1. Removed `final` from `ABP_FirstPersonPlayerState_Online_C` (C3246 inheritance bug in Dumper-7)
2. Added 128 empty #define fallback macros in Assertions.inl for missing cLeon/Decoy/Hunter/Survivor SDK assertions (C4430 errors)
3. Added `/utf-8` compile option in vcxproj for both Debug|x64 and Release|x64 configs
4. Localized Menu.cpp and CheatManager.cpp interface text to Chinese

## Compile Notes
- Use `/utf-8` flag when compiling with cl.exe manually (Chinese string literals are UTF-8)
- SDK .cpp files include Basic.hpp → Assertions.inl, not includes.hpp; so fallback macros must go in Assertions.inl
- Warnings C4309/C4369 (enum truncation) are suppressed via DisableSpecificWarnings in vcxproj
- Build via: F:\A\meccha-esp\chameleonEsp-main\build.bat (uses MSBuild Release|x64)
- ⚠️ When editing vcxproj XML, always verify tag closure — previous Edit accidentally removed Release|x64 closing tags (</ClCompile>, </Link>, </Manifest>, </ItemDefinitionGroup>) causing MSB4025 error. Fixed 2026-07-24.
