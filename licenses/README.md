# Third-party components

| Component | Notice |
|---|---|
| MinHook | MinHook-LICENSE.txt |
| NVIDIA NGX / DLSS / neural-rendering runtime | NVIDIA-RTX-SDK-LICENSE.txt |
| NVIDIA Streamline | Streamline-LICENSE.txt, Streamline-THIRD-PARTY-NOTICES.md |
| AMD FidelityFX RCAS | AMD-RCAS-LICENSE.txt |

The project's MIT license covers original project code only. Third-party components retain their respective licenses and copyright notices.

MinHook (including its disassembler notices), AMD RCAS, and the Streamline components retain the notices listed above. NVIDIA NGX loader, DLSS SR and NR binaries are governed by NVIDIA-RTX-SDK-LICENSE.txt, not the project's MIT license. The NR SDK archive includes the same NVIDIA RTX SDK license.

The NVIDIA SDK license permits object-code integration into applications with material additional functionality, subject to its distribution requirements and supplement. Recipients must observe that license when using or redistributing the NVIDIA components; the project's MIT grant does not grant rights to those components. SDK attribution, applicable NVIDIA Marks requirements, and notification before commercial release remain applicable under that license. Moving a DLL to a user-supplied dependency does not remove obligations for SDK code linked into the ASI.

The specific redistribution terms for nvngx_dlssg.dll could not be established from the Streamline 2.14.16 archive, so new installation packages exclude that file. Users must obtain it from the official SDK as described in the README. Its presence in the SDK is not treated as an MIT grant.

Bundled SDK and model binaries are unchanged and are not stored in Git. Local build output may include nvngx_dlssg.dll for development; the packaging script excludes it. Previously generated archives are historical artifacts and have not been repackaged.
