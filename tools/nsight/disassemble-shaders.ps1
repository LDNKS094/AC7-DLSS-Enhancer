param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class ShaderAssembly {
    [ComImport, Guid("8BA5FB08-5195-40e2-AC58-0D989C3A0102"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface Blob {
        [PreserveSig] IntPtr GetBufferPointer();
        [PreserveSig] UIntPtr GetBufferSize();
    }
    [DllImport("d3dcompiler_47.dll", CallingConvention=CallingConvention.StdCall)]
    private static extern int D3DDisassemble(byte[] source, UIntPtr size, uint flags, string comments, out Blob output);
    public static void Export(string input) {
        byte[] bytes = File.ReadAllBytes(input);
        Blob result;
        int hr = D3DDisassemble(bytes, (UIntPtr)bytes.Length, 0, null, out result);
        Marshal.ThrowExceptionForHR(hr);
        try {
            int size = checked((int)result.GetBufferSize().ToUInt64());
            byte[] text = new byte[size];
            Marshal.Copy(result.GetBufferPointer(), text, 0, size);
            File.WriteAllBytes(Path.ChangeExtension(input, ".asm"), text);
        } finally { Marshal.ReleaseComObject(result); }
    }
}
'@
$shaderFiles = @(Get-ChildItem -LiteralPath $Directory -Filter '*.dxbc' -File)
if ($shaderFiles.Count -eq 0) { throw 'No extracted shaders found' }
foreach ($shaderFile in $shaderFiles) { [ShaderAssembly]::Export($shaderFile.FullName) }
Write-Output "PASS: disassembled $($shaderFiles.Count) DXBC shaders using D3DDisassemble."
