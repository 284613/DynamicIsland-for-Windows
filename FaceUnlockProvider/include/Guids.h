#pragma once
#include <guiddef.h>

// Credential Provider CLSID — fixed for the lifetime of this product.
// Registered at:
//   HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Authentication\Credential Providers\
//     {7B3C9F2E-4D8A-4E1B-9C6D-DA5F1E80B742}
//
// {7B3C9F2E-4D8A-4E1B-9C6D-DA5F1E80B742}
// DEFINE_GUID is intentionally placed in a header guard so multiple TUs see the
// same declaration; instantiation lives in dllmain.cpp via INITGUID.
DEFINE_GUID(CLSID_FaceCredentialProvider,
            0x7b3c9f2e, 0x4d8a, 0x4e1b, 0x9c, 0x6d, 0xda, 0x5f, 0x1e, 0x80, 0xb7, 0x42);
